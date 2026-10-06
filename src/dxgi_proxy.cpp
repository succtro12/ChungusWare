#include <windows.h>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <intrin.h>
extern "C" { void* dxgi_exports[20]{}; }
namespace {
HMODULE module,systemDxgi;
std::once_flag captureOnce;
thread_local bool bypass=false;
void ensureCapture(void* caller){
#ifdef FG_LOADER_TEST
    return;
#endif
    if(bypass)return;
    HMODULE owner=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCWSTR>(caller),&owner);
    wchar_t ownerPath[32768]{};GetModuleFileNameW(owner,ownerPath,32768);
    // The capture worker uses the real factory to discover hook methods. It
    // must not wait on itself when its imports resolve through this proxy.
    if(!_wcsicmp(std::filesystem::path(ownerPath).filename().c_str(),L"bedrock_rr_capture.dll"))return;
    std::call_once(captureOnce,[]{
        wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);auto dir=std::filesystem::path(path).parent_path();
        if(std::filesystem::exists(dir/L"bedrock_rr_discovery.dll")){
            auto discovery=LoadLibraryW((dir/L"bedrock_rr_discovery.dll").c_str());
            auto wait=discovery?reinterpret_cast<BOOL(WINAPI*)(DWORD)>(GetProcAddress(discovery,"NgxDiscoveryWait")):nullptr;
            if(!wait||!wait(10000))return;
        }
        auto capture=LoadLibraryW((dir/L"bedrock_rr_capture.dll").c_str());if(!capture)return;
        auto wait=reinterpret_cast<BOOL(WINAPI*)(DWORD)>(GetProcAddress(capture,"BedrockRrWaitUntilReady"));
        if(wait)wait(10000);
        // Separate observer only records the existing NGX inputs. It never
        // replaces SR with RR unless a verified renderer bridge exists.
        LoadLibraryW((dir/L"bedrock_rr_ngx_observer.dll").c_str());
        if(std::filesystem::exists(dir/L"bedrock_rr_bridge.dll"))LoadLibraryW((dir/L"bedrock_rr_bridge.dll").c_str());
        if(std::filesystem::exists(dir/L"bedrock_rr_activation.dll"))LoadLibraryW((dir/L"bedrock_rr_activation.dll").c_str());
        if(std::filesystem::exists(dir/L"bedrock_rr_quality.dll"))LoadLibraryW((dir/L"bedrock_rr_quality.dll").c_str());
    });
}
void upgradeFactory(HRESULT result,void** factory){
    if(bypass||FAILED(result)||!factory||!*factory)return;
    auto bridge=GetModuleHandleW(
#ifdef FG_LOADER_TEST
        nullptr
#else
        L"bedrock_rr_bridge.dll"
#endif
    );
    auto upgrade=bridge?reinterpret_cast<BOOL(WINAPI*)(void**)>(GetProcAddress(bridge,"FgUpgradeFactory")):nullptr;
    if(upgrade)upgrade(factory);
}
}
extern "C" void WINAPI BedrockRrBypassForThread(BOOL value){bypass=value!=FALSE;}
extern "C" HRESULT WINAPI WrappedFactory(REFIID iid,void** out){ensureCapture(_ReturnAddress());auto hr=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(dxgi_exports[3])(iid,out);upgradeFactory(hr,out);return hr;}
extern "C" HRESULT WINAPI WrappedFactory1(REFIID iid,void** out){ensureCapture(_ReturnAddress());auto hr=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(dxgi_exports[4])(iid,out);upgradeFactory(hr,out);return hr;}
extern "C" HRESULT WINAPI WrappedFactory2(UINT flags,REFIID iid,void** out){ensureCapture(_ReturnAddress());auto hr=reinterpret_cast<HRESULT(WINAPI*)(UINT,REFIID,void**)>(dxgi_exports[5])(flags,iid,out);upgradeFactory(hr,out);return hr;}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;module=h;DisableThreadLibraryCalls(h);
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);auto path=std::filesystem::path(system)/L"dxgi.dll";
    systemDxgi=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!systemDxgi||systemDxgi==module)return FALSE;
    const char* names[]={"ApplyCompatResolutionQuirking","CompatString","CompatValue","CreateDXGIFactory","CreateDXGIFactory1","CreateDXGIFactory2",
        "DXGID3D10CreateDevice","DXGID3D10CreateLayeredDevice","DXGID3D10GetLayeredDeviceSize","DXGID3D10RegisterLayers","DXGIDeclareAdapterRemovalSupport",
        "DXGIDisableVBlankVirtualization","DXGIDumpJournal","DXGIGetDebugInterface1","DXGIReportAdapterConfiguration","PIXBeginCapture","PIXEndCapture",
        "PIXGetCaptureState","SetAppCompatStringPointer","UpdateHMDEmulationStatus"};
    for(unsigned i=0;i<20;i++){dxgi_exports[i]=reinterpret_cast<void*>(GetProcAddress(systemDxgi,names[i]));if(!dxgi_exports[i])return FALSE;}
    return TRUE;
}
