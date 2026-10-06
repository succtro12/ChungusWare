#include <windows.h>
#include <evntrace.h>
#include <string>
#include <vector>
// Separate lifetime guard: an injected DLL cannot clean up ETW after its host
// has terminated. No rendering or presentation work happens in this process.
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;DWORD parentId=wcstoul(argv[1],nullptr,10),childId=wcstoul(argv[2],nullptr,10);
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parentId),child=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,childId);
    if(!child){if(parent)CloseHandle(parent);return 0;}
    HANDLE handles[]={parent,child};DWORD result=parent?WaitForMultipleObjects(2,handles,FALSE,INFINITE):WAIT_OBJECT_0;
    if(result==WAIT_OBJECT_0){
        auto name=L"BedrockDelivery"+std::to_wstring(parentId);std::vector<unsigned char> storage(sizeof(EVENT_TRACE_PROPERTIES)+(name.size()+1)*sizeof(wchar_t));
        auto properties=reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data());properties->Wnode.BufferSize=ULONG(storage.size());properties->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
        ControlTraceW(0,name.c_str(),properties,EVENT_TRACE_CONTROL_STOP);
        if(WaitForSingleObject(child,2000)==WAIT_TIMEOUT)TerminateProcess(child,0);
    }
    if(parent)CloseHandle(parent);CloseHandle(child);return 0;
}
