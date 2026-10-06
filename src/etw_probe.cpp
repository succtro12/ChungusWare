#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <map>
#include <string>
#include <cstdio>
#include <mutex>
#include <vector>
std::mutex guard;
DWORD targetPid=0;unsigned long long total=0,own=0;
std::map<std::wstring,std::map<unsigned,unsigned>> counts;
std::map<std::wstring,std::map<unsigned,unsigned>> allCounts;
void WINAPI event(EVENT_RECORD* r){
 std::lock_guard lock(guard);total++;wchar_t g[64]{};StringFromGUID2(r->EventHeader.ProviderId,g,64);allCounts[g][r->EventHeader.EventDescriptor.Id]++;
 if(r->EventHeader.ProcessId!=targetPid)return;own++;counts[g][r->EventHeader.EventDescriptor.Id]++;
}
int wmain(int argc,wchar_t** argv){
 if(argc<3)return 2;targetPid=wcstoul(argv[2],nullptr,10);
 TRACEHANDLE session=0;std::vector<unsigned char> properties;
 if(argc>4&&wcscmp(argv[4],L"--own-session")==0){
  properties.resize(sizeof(EVENT_TRACE_PROPERTIES)+512);auto p=reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties.data());
  p->Wnode.BufferSize=DWORD(properties.size());p->Wnode.Flags=WNODE_FLAG_TRACED_GUID;p->Wnode.ClientContext=1;
  p->BufferSize=64;p->MinimumBuffers=32;p->MaximumBuffers=128;p->LogFileMode=EVENT_TRACE_REAL_TIME_MODE;p->FlushTimer=1;p->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
  auto result=StartTraceW(&session,argv[1],p);printf("StartTrace=%lu\n",result);if(result!=ERROR_SUCCESS)return 1;
  GUID kernel={0x802ec45a,0x1e99,0x4b83,{0x99,0x20,0x87,0xc9,0x82,0x77,0xba,0x9d}};
  result=EnableTraceEx2(session,&kernel,EVENT_CONTROL_CODE_ENABLE_PROVIDER,5,0x8000841,0,0,nullptr);printf("EnableTraceEx2=%lu\n",result);
 }
 EVENT_TRACE_LOGFILEW log{};log.LoggerName=argv[1];log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;log.EventRecordCallback=&event;
 auto h=OpenTraceW(&log);if(h==INVALID_PROCESSTRACE_HANDLE){printf("OpenTrace error %lu\n",GetLastError());return 1;}
 auto thread=CreateThread(nullptr,0,[](void* p)->DWORD{auto h=*static_cast<TRACEHANDLE*>(p);return ProcessTrace(&h,1,nullptr,nullptr);},&h,0,nullptr);
 Sleep((argc>3?wcstoul(argv[3],nullptr,10):15)*1000);CloseTrace(h);WaitForSingleObject(thread,2000);CloseHandle(thread);
 if(session)ControlTraceW(session,argv[1],reinterpret_cast<EVENT_TRACE_PROPERTIES*>(properties.data()),EVENT_TRACE_CONTROL_STOP);
 std::lock_guard lock(guard);printf("Total %llu; target PID %lu events %llu\n",total,targetPid,own);
 for(auto& [g,events]:counts)for(auto [id,count]:events)printf("%ls id=%u count=%u\n",g.c_str(),id,count);
 puts("All graphics event headers (counts only):");for(auto& [g,events]:allCounts)for(auto [id,count]:events)printf("%ls id=%u count=%u\n",g.c_str(),id,count);return 0;
}
