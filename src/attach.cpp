#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>
static bool inject(DWORD pid,const std::filesystem::path& dll) {
    HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
    if(!process){std::cerr<<"Cannot open Minecraft: "<<GetLastError()<<"\n";return false;}
    wchar_t image[32768]{};DWORD length=32768;
    if(!QueryFullProcessImageNameW(process,0,image,&length)||_wcsicmp(std::filesystem::path(image).filename().c_str(),L"Minecraft.Windows.exe")) {
        std::cerr<<"Target must be Minecraft.Windows.exe\n";CloseHandle(process);return false;
    }
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    MODULEENTRY32W me{};me.dwSize=sizeof(me);bool present=false;
    if(modules!=INVALID_HANDLE_VALUE){if(Module32FirstW(modules,&me))do{if(!_wcsicmp(me.szModule,dll.filename().c_str()))present=true;}while(Module32NextW(modules,&me));CloseHandle(modules);}
    if(present){std::cout<<"Capture module already attached. Restart Minecraft for a fresh capture.\n";CloseHandle(process);return true;}
    auto path=dll.wstring();SIZE_T bytes=(path.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote||!WriteProcessMemory(process,remote,path.c_str(),bytes,nullptr)) {std::cerr<<"Cannot copy module path: "<<GetLastError()<<"\n";if(remote)VirtualFreeEx(process,remote,0,MEM_RELEASE);CloseHandle(process);return false;}
    // Resolve LoadLibraryW using its module-relative offset in the target, rather
    // than assuming the same ASLR address in both processes.
    auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");
    HMODULE owner=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner);
    wchar_t ownerPath[MAX_PATH]{};GetModuleFileNameW(owner,ownerPath,MAX_PATH);
    uintptr_t remoteBase=0;modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(modules!=INVALID_HANDLE_VALUE){me.dwSize=sizeof(me);if(Module32FirstW(modules,&me))do{if(!_wcsicmp(me.szModule,std::filesystem::path(ownerPath).filename().c_str()))remoteBase=reinterpret_cast<uintptr_t>(me.modBaseAddr);}while(Module32NextW(modules,&me));CloseHandle(modules);}
    HANDLE thread=nullptr;
    if(remoteBase)thread=CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase+reinterpret_cast<uintptr_t>(load)-reinterpret_cast<uintptr_t>(owner)),remote,0,nullptr);
    if(!thread){std::cerr<<"Cannot load capture module: "<<GetLastError()<<"\n";VirtualFreeEx(process,remote,0,MEM_RELEASE);CloseHandle(process);return false;}
    DWORD wait=WaitForSingleObject(thread,15000),result=0;
    if(wait==WAIT_OBJECT_0){GetExitCodeThread(thread,&result);VirtualFreeEx(process,remote,0,MEM_RELEASE);}
    // A timed-out loader may still read its argument; keep that allocation alive.
    CloseHandle(thread);CloseHandle(process);
    if(wait!=WAIT_OBJECT_0||!result){std::cerr<<"Module load failed or timed out.\n";return false;}
    std::cout<<"Native capture attached to Minecraft. Capture status is in the capture folder.\n";return true;
}
int wmain(int argc,wchar_t** argv) {
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    auto dll=std::filesystem::path(exe).parent_path()/L"bedrock_rr_capture.dll";
    bool launch=argc>2&&!wcscmp(argv[1],L"--launch");
    if(argc>1&&!launch)dll=std::filesystem::absolute(argv[1]);
    if(!std::filesystem::exists(dll)){std::cerr<<"Capture DLL missing\n";return 1;}
    DWORD pid=0;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W pe{};pe.dwSize=sizeof(pe);
    if(snap!=INVALID_HANDLE_VALUE){if(Process32FirstW(snap,&pe))do{if(!_wcsicmp(pe.szExeFile,L"Minecraft.Windows.exe")){if(pid){std::cerr<<"Multiple Minecraft instances; close extras first\n";CloseHandle(snap);return 1;}pid=pe.th32ProcessID;}}while(Process32NextW(snap,&pe));CloseHandle(snap);}
    if(launch){
        if(pid){std::cerr<<"Close Minecraft first so capture starts before renderer initialization.\n";return 2;}
        auto game=std::filesystem::absolute(argv[2]);
        if(_wcsicmp(game.filename().c_str(),L"Minecraft.Windows.exe")||!std::filesystem::exists(game)){std::cerr<<"Invalid Minecraft executable\n";return 1;}
        std::wstring cmd=L"\""+game.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(game.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,game.parent_path().c_str(),&startup,&process)){
            std::cerr<<"Minecraft launch failed: "<<GetLastError()<<"\n";return 1;
        }
        // Preserve prior captures and remove the previous status sentinel, so
        // initialization completion cannot be confused with an older run.
        auto capture=dll.parent_path()/L"capture";
        if(std::filesystem::exists(capture)){auto archived=dll.parent_path()/(L"capture-"+std::to_wstring(GetTickCount64()));std::filesystem::rename(capture,archived);}
        bool loaded=inject(process.dwProcessId,dll),ready=false;
        if(loaded)for(unsigned i=0;i<100;i++){
            std::ifstream file(capture/L"status.json");std::string text((std::istreambuf_iterator<char>(file)),{});
            if(text.find("\"ready\"")!=std::string::npos){ready=true;break;}
            if(text.find("\"failed\"")!=std::string::npos)break;
            Sleep(100);
        }
        if(ready)std::cout<<"Capture initialized before Minecraft startup.\n";
        else std::cerr<<"Early capture not ready; Minecraft will still start. Check capture/status.json.\n";
        ResumeThread(process.hThread);CloseHandle(process.hThread);CloseHandle(process.hProcess);return ready?0:1;
    }
    if(!pid){std::cerr<<"Open Minecraft for Windows, then run Attach capture again before entering an RTX world.\n";return 2;}
    return inject(pid,dll)?0:1;
}
