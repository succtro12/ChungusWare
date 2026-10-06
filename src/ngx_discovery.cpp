#include <windows.h>
#include <d3d12.h>
#include <MinHook.h>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstring>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs_dlssd.h"
namespace discovery {
HMODULE module;
HANDLE readyEvent;
std::ofstream file;
std::mutex logMutex;
thread_local bool logging=false;
std::atomic_bool started{false};
decltype(&GetProcAddress) realGetProc=nullptr;
decltype(&LoadLibraryExW) realLoad=nullptr;
struct UnicodeName {USHORT Length;USHORT MaximumLength;PWSTR Buffer;};
using LdrLoad=LONG(NTAPI*)(PWSTR,ULONG*,UnicodeName*,HMODULE*);
LdrLoad realLdrLoad=nullptr;
// These are the DRIVER export ABIs, not the static SDK frontend signatures.
// Driver disassembly verifies SDK version in R9 and common info at stack +0x28.
using InitExt=NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long,const wchar_t*,ID3D12Device*,unsigned,const NVSDK_NGX_FeatureCommonInfo*);
using InitBase=NVSDK_NGX_Result(NVSDK_CONV*)(unsigned long long,const wchar_t*,ID3D12Device*,unsigned);
using InitProject=NVSDK_NGX_Result(NVSDK_CONV*)(const char*,NVSDK_NGX_EngineType,const char*,const wchar_t*,ID3D12Device*,unsigned,const NVSDK_NGX_FeatureCommonInfo*);
InitExt realExt=nullptr;InitBase realBase=nullptr;InitProject realProject=nullptr;
std::mutex initHookMutex;
HMODULE hookedRuntime=nullptr;
void hookRuntime(HMODULE h);
NVSDK_NGX_AppLogCallback previousLog=nullptr;
std::string quote(const std::string& s){std::string o="\"";for(unsigned char c:s){if(c=='\\'||c=='\"'){o+='\\';o+=c;}else if(c=='\n')o+="\\n";else if(c=='\r')o+="\\r";else if(c=='\t')o+="\\t";else if(c>=32)o+=c;}return o+'\"';}
std::string utf8(const wchar_t* s){if(!s)return "";int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);std::string out(n?size_t(n):0,0);if(n)WideCharToMultiByte(CP_UTF8,0,s,-1,out.data(),n,nullptr,nullptr);if(!out.empty())out.pop_back();return out;}
std::string hex(uint64_t n){std::ostringstream s;s<<"0x"<<std::hex<<n;return s.str();}
std::string result(NVSDK_NGX_Result r){return hex(unsigned(r));}
void emit(const std::string& s){bool old=logging;logging=true;{std::lock_guard guard(logMutex);file<<s<<'\n';file.flush();}logging=old;}
std::wstring modulePath(HMODULE h){wchar_t p[32768]{};GetModuleFileNameW(h,p,32768);return p;}
std::string site(void* p){HMODULE h=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(p),&h))return hex(uintptr_t(p));return std::filesystem::path(modulePath(h)).filename().string()+"+"+hex(uintptr_t(p)-uintptr_t(h));}
std::string version(const std::wstring& p){DWORD dummy=0,n=GetFileVersionInfoSizeW(p.c_str(),&dummy);if(!n)return "unavailable";std::vector<char> b(n);if(!GetFileVersionInfoW(p.c_str(),0,n,b.data()))return "unavailable";VS_FIXEDFILEINFO* v=nullptr;UINT len=0;if(!VerQueryValueW(b.data(),L"\\",reinterpret_cast<void**>(&v),&len)||len<sizeof(*v))return "unavailable";return std::to_string(HIWORD(v->dwFileVersionMS))+"."+std::to_string(LOWORD(v->dwFileVersionMS))+"."+std::to_string(HIWORD(v->dwFileVersionLS))+"."+std::to_string(LOWORD(v->dwFileVersionLS));}
void NVSDK_CONV ngxLog(const char* message,NVSDK_NGX_Logging_Level level,NVSDK_NGX_Feature component){
    if(previousLog&&previousLog!=&ngxLog)previousLog(message,level,component);
    // Keep model-selection evidence even after the general startup log budget.
    // Runtime discovery and provider selection are unchanged.
    bool modelEvidence=message&&(strstr(message,"Preset")||strstr(message,"preset")||strstr(message,"RR2"));
    static std::atomic_uint lines{0};if(lines++>=4000&&!modelEvidence)return;
    try{emit("{\"event\":\"ngx_runtime_log\",\"level\":"+std::to_string(level)+",\"feature\":"+std::to_string(component)+",\"message\":"+quote(message?message:"")+"}");}catch(...) {}
}
std::wstring runtimeDirectory(){auto path=modulePath(module);if(path.empty())throw std::runtime_error("NGX module path unavailable");return std::filesystem::weakly_canonical(std::filesystem::path(path).parent_path()).wstring();}
struct Info {
    NVSDK_NGX_FeatureCommonInfo value{};
    std::vector<const wchar_t*> paths;
    std::wstring additional;
    Info(const NVSDK_NGX_FeatureCommonInfo* original,unsigned sdk){
        if(original){value.PathListInfo=original->PathListInfo;if(sdk>=0x13)value.InternalData=original->InternalData;if(sdk>=0x14)value.LoggingInfo=original->LoggingInfo;
            if(original->PathListInfo.Length>128)throw std::runtime_error("Unreasonable native NGX path count");
            for(unsigned i=0;i<original->PathListInfo.Length;i++)paths.push_back(original->PathListInfo.Path[i]);}
        additional=runtimeDirectory();
        if(std::filesystem::exists(std::filesystem::path(additional)/L"nvngx_dlssd.dll"))paths.push_back(additional.c_str());
        value.PathListInfo={paths.data(),unsigned(paths.size())};
        // Existing sinks remain enabled. Runtime callback provides the concrete
        // NGX library search and provider initialization diagnostics.
        if(sdk>=0x14){previousLog=value.LoggingInfo.LoggingCallback;value.LoggingInfo.LoggingCallback=&ngxLog;value.LoggingInfo.MinimumLoggingLevel=NVSDK_NGX_LOGGING_LEVEL_VERBOSE;}
    }
};
void logInit(const char* api,void* caller,unsigned sdk,const wchar_t* data,ID3D12Device* device,const NVSDK_NGX_FeatureCommonInfo* original,const Info& info,const std::string& identity){
    auto luid=device?device->GetAdapterLuid():LUID{};std::ostringstream s;
    s<<"{\"event\":\"ngx_init\",\"api\":"<<quote(api)<<",\"return_address\":"<<quote(site(caller))<<",\"sdk_version\":"<<quote(hex(sdk))<<",\"application_data_path\":"<<quote(utf8(data))<<",\"identity\":"<<identity
     <<",\"adapter_luid\":"<<quote(hex((uint64_t(unsigned(luid.HighPart))<<32)|luid.LowPart))<<",\"original_paths\":[";
    if(original)for(unsigned i=0;i<original->PathListInfo.Length;i++){if(i)s<<',';s<<quote(utf8(original->PathListInfo.Path[i]));}
    s<<"],\"effective_paths\":[";for(size_t i=0;i<info.paths.size();i++){if(i)s<<',';s<<quote(utf8(info.paths[i]));}s<<"]}";emit(s.str());
}
void afterInit(NVSDK_NGX_Result r){
    emit("{\"event\":\"ngx_init_result\",\"result\":"+quote(result(r))+"}");if(NVSDK_NGX_FAILED(r))return;
    auto ngx=GetModuleHandleW(L"_nvngx.dll");
    auto get=reinterpret_cast<decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters)>(realGetProc(ngx,"NVSDK_NGX_D3D12_GetCapabilityParameters"));
    auto destroy=reinterpret_cast<decltype(&NVSDK_NGX_D3D12_DestroyParameters)>(realGetProc(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));
    NVSDK_NGX_Parameter* p=nullptr;if(!get||!destroy)return;auto q=get(&p);int rr=-1,sr=-1;
    auto rq=p?p->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&rr):NVSDK_NGX_Result_FAIL_NotInitialized;
    if(p)p->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&sr);
    auto provider=GetModuleHandleW(L"nvngx_dlssd.dll");auto path=provider?modulePath(provider):L"";
    emit("{\"event\":\"support_after_init\",\"capability_result\":"+quote(result(q))+",\"rr_query_result\":"+quote(result(rq))+",\"rr_available\":"+std::to_string(rr)+",\"sr_available\":"+std::to_string(sr)+",\"rr_provider_path\":"+quote(utf8(path.c_str()))+",\"rr_provider_version\":"+quote(provider?version(path):"not_loaded")+"}");
    if(p)destroy(p);
}
NVSDK_NGX_Result NVSDK_CONV initExt(unsigned long long id,const wchar_t* data,ID3D12Device* device,unsigned sdk,const NVSDK_NGX_FeatureCommonInfo* original){
    try{Info info(original,sdk);logInit("NVSDK_NGX_D3D12_Init_Ext",_ReturnAddress(),sdk,data,device,original,info,std::to_string(id));auto r=realExt(id,data,device,sdk,&info.value);afterInit(r);return r;}catch(...){emit("{\"event\":\"init_augmentation_exception\"}");return realExt(id,data,device,sdk,original);}
}
NVSDK_NGX_Result NVSDK_CONV initBase(unsigned long long id,const wchar_t* data,ID3D12Device* device,unsigned sdk){
    try{Info info(nullptr,sdk);logInit("NVSDK_NGX_D3D12_Init -> Init_Ext",_ReturnAddress(),sdk,data,device,nullptr,info,std::to_string(id));auto r=realExt?realExt(id,data,device,sdk,&info.value):realBase(id,data,device,sdk);afterInit(r);return r;}catch(...){return realBase(id,data,device,sdk);}
}
NVSDK_NGX_Result NVSDK_CONV initProject(const char* id,NVSDK_NGX_EngineType engine,const char* engineVersion,const wchar_t* data,ID3D12Device* device,unsigned sdk,const NVSDK_NGX_FeatureCommonInfo* original){
    try{Info info(original,sdk);logInit("NVSDK_NGX_D3D12_Init_ProjectID",_ReturnAddress(),sdk,data,device,original,info,quote(id?id:""));auto r=realProject(id,engine,engineVersion,data,device,sdk,&info.value);afterInit(r);return r;}catch(...){return realProject(id,engine,engineVersion,data,device,sdk,original);}
}
FARPROC WINAPI getProc(HMODULE h,LPCSTR name){
    auto p=realGetProc(h,name);if(logging||uintptr_t(name)<65536||!p)return p;
    if(strncmp(name,"NVSDK_NGX_D3D12_Init",19))return p;
    if(_wcsicmp(std::filesystem::path(modulePath(h)).filename().c_str(),L"_nvngx.dll"))return p;
    emit("{\"event\":\"ngx_init_export_resolution\",\"export\":"+quote(name)+",\"resolver_return_address\":"+quote(site(_ReturnAddress()))+",\"module_path\":"+quote(utf8(modulePath(h).c_str()))+"}");
    hookRuntime(h);
    return p;
}
void hookRuntime(HMODULE h){
    if(!h)return;std::lock_guard guard(initHookMutex);if(hookedRuntime==h)return;
    struct Entry {const char* name;void* wrapper;void** original;};
    Entry entries[]={
        {"NVSDK_NGX_D3D12_Init_Ext",reinterpret_cast<void*>(&initExt),reinterpret_cast<void**>(&realExt)},
        {"NVSDK_NGX_D3D12_Init",reinterpret_cast<void*>(&initBase),reinterpret_cast<void**>(&realBase)},
        {"NVSDK_NGX_D3D12_Init_ProjectID",reinterpret_cast<void*>(&initProject),reinterpret_cast<void**>(&realProject)}};
    for(auto& entry:entries){auto address=realGetProc(h,entry.name);auto status=address?MH_CreateHook(address,entry.wrapper,entry.original):MH_ERROR_FUNCTION_NOT_FOUND;
        if(status==MH_OK)status=MH_EnableHook(address);
        emit("{\"event\":\"ngx_init_direct_hook\",\"export\":"+quote(entry.name)+",\"address\":"+quote(site(reinterpret_cast<void*>(address)))+",\"hook_status\":"+std::to_string(status)+"}");}
    hookedRuntime=h;
}
HMODULE WINAPI load(LPCWSTR path,HANDLE h,DWORD flags){
    auto caller=_ReturnAddress();auto result=realLoad(path,h,flags);DWORD error=GetLastError();
    if(result&&path&&!logging&&!_wcsicmp(std::filesystem::path(path).filename().c_str(),L"_nvngx.dll"))hookRuntime(result);
    if(!logging&&path&&wcsstr(path,L"nvngx")){
        logging=true;auto loaded=result?modulePath(result):L"";
        emit("{\"event\":\"ngx_library_load\",\"requested_path\":"+quote(utf8(path))+",\"flags\":"+std::to_string(flags)+",\"caller_return_address\":"+quote(site(caller))+",\"loaded_path\":"+quote(utf8(loaded.c_str()))+",\"version\":"+quote(result?version(loaded):"not_loaded")+",\"win32_error\":"+std::to_string(result?0:error)+"}");logging=false;
    }
    SetLastError(error);return result;
}
LONG NTAPI loader(PWSTR search,ULONG* flags,UnicodeName* name,HMODULE* handle){
    auto caller=_ReturnAddress();auto status=realLdrLoad(search,flags,name,handle);
    if(status>=0&&handle&&*handle&&name&&!logging){
        std::wstring requested(name->Buffer,name->Length/sizeof(wchar_t));
        if(requested.find(L"nvngx")!=std::wstring::npos){
            auto actual=modulePath(*handle);
            emit("{\"event\":\"ngx_native_loader\",\"requested_path\":"+quote(utf8(requested.c_str()))+",\"loaded_path\":"+quote(utf8(actual.c_str()))+",\"caller_return_address\":"+quote(site(caller))+",\"status\":"+quote(hex(unsigned(status)))+"}");
            if(!_wcsicmp(std::filesystem::path(actual).filename().c_str(),L"_nvngx.dll"))hookRuntime(*handle);
        }
    }
    return status;
}
bool start(){
    if(started)return true;auto dir=std::filesystem::path(modulePath(module)).parent_path()/L"discovery";std::filesystem::create_directories(dir);file.open(dir/L"events.jsonl",std::ios::trunc);
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto kernel=GetModuleHandleW(L"kernel32.dll");auto a=GetProcAddress(kernel,"GetProcAddress"),b=GetProcAddress(kernel,"LoadLibraryExW");
    if(MH_CreateHook(a,reinterpret_cast<void*>(&getProc),reinterpret_cast<void**>(&realGetProc))!=MH_OK)return false;
    if(MH_CreateHook(b,reinterpret_cast<void*>(&load),reinterpret_cast<void**>(&realLoad))!=MH_OK){MH_RemoveHook(a);return false;}
    auto c=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"LdrLoadDll");
    if(MH_CreateHook(c,reinterpret_cast<void*>(&loader),reinterpret_cast<void**>(&realLdrLoad))!=MH_OK){MH_RemoveHook(a);MH_RemoveHook(b);return false;}
    MH_QueueEnableHook(a);MH_QueueEnableHook(b);MH_QueueEnableHook(c);if(MH_ApplyQueued()!=MH_OK){MH_DisableHook(a);MH_DisableHook(b);MH_DisableHook(c);return false;}
    hookRuntime(GetModuleHandleW(L"_nvngx.dll"));
    started=true;emit("{\"event\":\"discovery_ready\",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"sr_replaced\":false}");SetEvent(readyEvent);return true;
}
DWORD WINAPI worker(void*){wchar_t p[32768]{};GetModuleFileNameW(nullptr,p,32768);auto name=std::filesystem::path(p).filename();if(_wcsicmp(name.c_str(),L"Minecraft.Windows.exe")&&_wcsicmp(name.c_str(),L"rr_capture_test.exe"))return 0;start();return 0;}
}
extern "C" __declspec(dllexport) bool NgxDiscoveryStart(){return discovery::start();}
extern "C" __declspec(dllexport) BOOL WINAPI NgxDiscoveryWait(DWORD ms){return WaitForSingleObject(discovery::readyEvent,ms)==WAIT_OBJECT_0;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){discovery::module=h;discovery::readyEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DisableThreadLibraryCalls(h);auto thread=CreateThread(nullptr,0,discovery::worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
