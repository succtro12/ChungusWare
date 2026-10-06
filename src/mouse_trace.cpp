#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <intrin.h>

namespace {
std::atomic<unsigned> rawDataCalls{0}, rawBufferCalls{0}, cursorCalls{0}, mouseMessages{0}, rawMessages{0}, pointerMessages{0};
decltype(&GetRawInputData) rawData=nullptr;
decltype(&GetRawInputBuffer) rawBuffer=nullptr;
decltype(&GetCursorPos) cursor=nullptr;
std::filesystem::path path;
std::mutex logMutex;
void caller(const char* api, unsigned index) {
    if(index>=4)return;
    void* frames[10]{};auto count=CaptureStackBackTrace(1,10,frames,nullptr);
    std::lock_guard guard(logMutex);std::ofstream log(path,std::ios::app);
    log<<"{\"api\":\""<<api<<"\",\"stack\":[";
    for(unsigned i=0;i<count;i++){
        HMODULE module=nullptr;wchar_t name[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frames[i]),&module);
        GetModuleFileNameW(module,name,MAX_PATH);
        if(i)log<<',';
        log<<"{\"module\":\""<<std::filesystem::path(name).filename().string()<<"\",\"offset\":"<<(uintptr_t(frames[i])-uintptr_t(module))<<"}";
    }log<<"]}\n";
}
UINT WINAPI data(HRAWINPUT handle,UINT command,LPVOID value,PUINT size,UINT header){caller("GetRawInputData",rawDataCalls.fetch_add(1));return rawData(handle,command,value,size,header);}
UINT WINAPI buffer(PRAWINPUT value,PUINT size,UINT header){caller("GetRawInputBuffer",rawBufferCalls.fetch_add(1));return rawBuffer(value,size,header);}
BOOL WINAPI position(LPPOINT point){caller("GetCursorPos",cursorCalls.fetch_add(1));return cursor(point);}
LRESULT CALLBACK messages(int code,WPARAM w,LPARAM l){
    if(code>=0&&l){auto m=reinterpret_cast<MSG*>(l);if(m->message==WM_MOUSEMOVE)mouseMessages++;if(m->message==WM_INPUT)rawMessages++;if(m->message==WM_POINTERUPDATE)pointerMessages++;}
    return CallNextHookEx(nullptr,code,w,l);
}
template<class T> void hook(HMODULE module,const char* name,void* replacement,T* original){
    auto address=GetProcAddress(module,name);auto result=MH_CreateHook(address,replacement,reinterpret_cast<void**>(original));
    if(result==MH_OK)result=MH_EnableHook(address);
    std::ofstream(path,std::ios::app)<<"{\"hook\":\""<<name<<"\",\"result\":"<<unsigned(result)<<"}\n";
}
DWORD WINAPI start(void* module){
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
    path=std::filesystem::path(exe).parent_path()/L"fg"/L"mouse-boundary-trace.jsonl";
    MH_Initialize();auto user=GetModuleHandleW(L"user32.dll");
    hook(user,"GetRawInputData",reinterpret_cast<void*>(&data),&rawData);
    hook(user,"GetRawInputBuffer",reinterpret_cast<void*>(&buffer),&rawBuffer);
    hook(user,"GetCursorPos",reinterpret_cast<void*>(&position),&cursor);
    HWND window=nullptr;EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w)){*reinterpret_cast<HWND*>(p)=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&window));
    DWORD thread=window?GetWindowThreadProcessId(window,nullptr):0;
    auto messageHook=thread?SetWindowsHookExW(WH_GETMESSAGE,messages,static_cast<HMODULE>(module),thread):nullptr;
    std::ofstream(path,std::ios::app)<<"{\"window_thread\":"<<thread<<",\"message_hook\":"<<(messageHook?"true":"false")<<"}\n";
    for(unsigned second=0;second<120;second++){
        Sleep(1000);std::lock_guard guard(logMutex);
        std::ofstream(path,std::ios::app)<<"{\"second\":"<<second<<",\"raw_data\":"<<rawDataCalls.load()<<",\"raw_buffer\":"<<rawBufferCalls.load()<<",\"cursor\":"<<cursorCalls.load()<<",\"mouse_messages\":"<<mouseMessages.load()<<",\"raw_messages\":"<<rawMessages.load()<<",\"pointer_messages\":"<<pointerMessages.load()<<"}\n";
    }
    if(messageHook)UnhookWindowsHookEx(messageHook);
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);auto t=CreateThread(nullptr,0,start,h,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
