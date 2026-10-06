#pragma once
#include <windows.h>
#include <evntrace.h>
#include <deque>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include "frame_generation.h"
// ETW is the delivery clock. Present timestamps below only correlate frame
// identity; a successful/queued Present is never treated as a displayed frame.
namespace delivery_telemetry {
struct Call {LONGLONG begin,end;uintptr_t chain;unsigned type;};
struct Event {double display,call,latency;unsigned type;int sync;bool delivered;};
std::mutex mutex;std::deque<Call> calls;std::deque<Event> events;
LONGLONG frequency=0,lastUpdate=0;unsigned unmatched=0,rows=0;
const char* status="Starting display telemetry";
HANDLE thread=nullptr,process=nullptr,job=nullptr,pipe=nullptr;
std::atomic<bool> stopping{false};std::filesystem::path root;
void record(LONGLONG begin,LONGLONG end,uintptr_t chain,unsigned type){std::lock_guard guard(mutex);calls.push_back({begin,end,chain,type});while(calls.size()>8192)calls.pop_front();}
std::vector<std::string> split(const std::string& s){std::vector<std::string> r;std::stringstream in(s);std::string field;while(std::getline(in,field,','))r.push_back(field);return r;}
void consume(const std::vector<std::string>& fields,const std::map<std::string,unsigned>& columns,std::ofstream& sequence){
    auto value=[&](const char* name)->std::string{auto i=columns.find(name);return i==columns.end()||i->second>=fields.size()?"":fields[i->second];};
    try{
        if(std::stoul(value("ProcessID"))!=GetCurrentProcessId())return;
        auto q=std::stoll(value("TimeInQPC"));auto chain=std::stoull(value("SwapChainAddress"),nullptr,0);
        unsigned type=0;LONGLONG closest=0;
        std::lock_guard guard(mutex);
        for(auto it=calls.rbegin();it!=calls.rend();++it)if(it->chain==chain&&it->begin<=q&&q<=it->end+frequency/20000&&it->begin>=closest){closest=it->begin;type=it->type;}
        if(!closest)unmatched++;
        bool delivered=value("MsUntilDisplayed")!="NA"&&!value("MsUntilDisplayed").empty();
        double call=double(q)*1000/frequency,latency=delivered?std::stod(value("MsUntilDisplayed")):0,display=call+latency;
        int sync=std::stoi(value("SyncInterval"));events.push_back({display,call,latency,type,sync,delivered});rows++;
        LARGE_INTEGER now{};QueryPerformanceCounter(&now);lastUpdate=now.QuadPart;
        double threshold=call-8000;while(!events.empty()&&events.front().call<threshold)events.pop_front();
        sequence<<q<<','<<(type==1?"REAL":type==2?"GENERATED":"UNKNOWN")<<','<<(delivered?std::to_string(display):"NA")<<','<<latency<<','<<sync<<','<<value("PresentMode")<<'\n';if(rows%120==0)sequence.flush();status="ETW display delivery (rolling 8 seconds)";
    }catch(...){}
}
DWORD WINAPI worker(void*){
    auto executable=root/L"bedrock-rr-streamline"/L"PresentMon.exe";
    if(!std::filesystem::exists(executable)){std::lock_guard guard(mutex);status="Display telemetry tool missing";return 1;}
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&sa,0)){std::lock_guard guard(mutex);status="Display telemetry pipe failed";return 1;}SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
    std::wstring command=L"\""+executable.wstring()+L"\" --process_id "+std::to_wstring(GetCurrentProcessId())+L" --terminate_on_proc_exit --stop_existing_session --output_stdout --no_console_stats --qpc_time --write_display_metadata --track_frame_type --session_name BedrockDelivery"+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdOutput=write;startup.hStdError=write;PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,root.c_str(),&startup,&child)){CloseHandle(write);CloseHandle(read);std::lock_guard guard(mutex);status="ETW process could not start";return 1;}
    // PresentMon owns its ETW session and exits cleanly when this process exits.
    // Killing it through a job object leaves an orphaned system trace session.
    HANDLE owner=nullptr;
    {std::lock_guard guard(mutex);process=child.hProcess;job=owner;pipe=read;status="Waiting for ETW display events";}
    ResumeThread(child.hThread);CloseHandle(child.hThread);CloseHandle(write);
    auto guardExe=root/L"bedrock-rr-streamline"/L"bedrock_delivery_watchdog.exe";
    std::wstring guardCommand=L"\""+guardExe.wstring()+L"\" "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(child.dwProcessId);
    STARTUPINFOW guardStartup{};guardStartup.cb=sizeof(guardStartup);PROCESS_INFORMATION guardProcess{};
    if(CreateProcessW(guardExe.c_str(),guardCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&guardStartup,&guardProcess)){CloseHandle(guardProcess.hThread);CloseHandle(guardProcess.hProcess);}
    std::ofstream sequence(root/L"fg"/L"display-events.csv");sequence<<"present_qpc,type,display_ms,display_latency_ms,sync_interval,present_mode\n";
    std::ofstream raw(root/L"fg"/L"display-etw.csv"),diagnostics(root/L"fg"/L"display-monitor.log");std::map<std::string,unsigned> columns;std::string pending;char buffer[16384];DWORD count=0;
    while(!stopping.load()&&ReadFile(read,buffer,sizeof(buffer),&count,nullptr)&&count){pending.append(buffer,count);size_t end;
        while((end=pending.find('\n'))!=std::string::npos){std::string line=pending.substr(0,end);pending.erase(0,end+1);if(!line.empty()&&line.back()=='\r')line.pop_back();auto fields=split(line);
            if(line.rfind("Application,",0)==0){for(unsigned i=0;i<fields.size();i++)columns[fields[i]]=i;raw<<line<<'\n';raw.flush();}
            else if(!columns.empty()&&fields.size()>=columns.size()){raw<<line<<'\n';consume(fields,columns,sequence);if(rows%120==0)raw.flush();}
            else {diagnostics<<line<<std::endl;}
        }
    }
    raw.flush();sequence.flush();{std::lock_guard guard(mutex);if(!stopping.load())status="ETW capture stopped (check display-etw.csv)";}return 0;
}
void start(const std::filesystem::path& directory){if(thread)return;root=directory;LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart;stopping=false;thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);}
void stop(){
    HANDLE child=nullptr;{std::lock_guard guard(mutex);child=process;}
    // Stop only our named ETW session, allowing the consumer to drain delivery
    // records. This runs during shutdown, never in the render/present loop.
    auto name=L"BedrockDelivery"+std::to_wstring(GetCurrentProcessId());
    std::vector<unsigned char> storage(sizeof(EVENT_TRACE_PROPERTIES)+(name.size()+1)*sizeof(wchar_t));
    auto properties=reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data());properties->Wnode.BufferSize=ULONG(storage.size());properties->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
    ControlTraceW(0,name.c_str(),properties,EVENT_TRACE_CONTROL_STOP);
    if(child&&WaitForSingleObject(child,2000)==WAIT_TIMEOUT)TerminateProcess(child,0);
    stopping=true;if(thread){CancelSynchronousIo(thread);WaitForSingleObject(thread,2000);CloseHandle(thread);thread=nullptr;}
    std::lock_guard guard(mutex);if(pipe)CloseHandle(pipe);if(process)CloseHandle(process);if(job)CloseHandle(job);pipe=process=job=nullptr;
}
FgDeliveryState state(){
    std::lock_guard guard(mutex);FgDeliveryState result{};result.status=status;result.rows=rows;result.unmatched=unmatched;
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);if(lastUpdate)result.ageSeconds=float(double(now.QuadPart-lastUpdate)/frequency);
    std::vector<Event> shown;unsigned generated=0;
    for(auto& e:events){if(!e.type)result.unknownFrames++;if(e.type==2&&!e.delivered)result.droppedGenerated++;if(e.delivered){shown.push_back(e);if(e.type==2)generated++;}}
    std::sort(shown.begin(),shown.end(),[](auto& a,auto& b){return a.display<b.display;});result.labelsComplete=result.unknownFrames==0;result.available=shown.size()>2&&result.ageSeconds<3;
    if(shown.size()<3)return result;double seconds=(shown.back().display-shown.front().display)/1000;if(seconds<=0)return result;
    result.generatedFps=float(generated/seconds);result.presentedFps=float((shown.size()-1)/seconds);result.syncInterval=shown.back().sync;
    std::vector<double> intervals;double total=0,rg=0,gr=0,latency=0;unsigned rgCount=0,grCount=0;
    for(unsigned i=0;i<shown.size();i++){latency+=shown[i].latency;if(!i)continue;double interval=shown[i].display-shown[i-1].display;intervals.push_back(interval);total+=interval;if(shown[i-1].type==1&&shown[i].type==2){rg+=interval;rgCount++;}if(shown[i-1].type==2&&shown[i].type==1){gr+=interval;grCount++;}}
    std::sort(intervals.begin(),intervals.end());result.meanMs=float(total/intervals.size());result.p99Ms=float(intervals[std::min(intervals.size()-1,size_t(intervals.size()*.99))]);result.worstMs=float(intervals.back());result.realToGeneratedMs=rgCount?float(rg/rgCount):0;result.generatedToRealMs=grCount?float(gr/grCount):0;result.displayLatencyMs=float(latency/shown.size());return result;
}
}
