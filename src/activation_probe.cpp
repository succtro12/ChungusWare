#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <atomic>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs_dlssd.h"
using Microsoft::WRL::ComPtr;
namespace {
HMODULE module;
std::mutex logMutex;
std::ofstream logFile;
std::atomic_bool attempted{false};
bool started=false;
decltype(&NVSDK_NGX_D3D12_CreateFeature) originalCreate=nullptr;
decltype(&NVSDK_NGX_D3D12_EvaluateFeature) originalEvaluate=nullptr;
decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters) getCapabilities=nullptr;
decltype(&NVSDK_NGX_D3D12_DestroyParameters) destroyParameters=nullptr;
decltype(&NVSDK_NGX_D3D12_ReleaseFeature) releaseFeature=nullptr;
std::string hex(uint64_t n){std::ostringstream s;s<<"0x"<<std::hex<<n;return s.str();}
std::string site(void* address){
    HMODULE owner=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(address),&owner))return hex(uintptr_t(address));
    GetModuleFileNameW(owner,path,32768);
    return std::filesystem::path(path).filename().string()+"+"+hex(uintptr_t(address)-uintptr_t(owner));
}
void emit(const std::string& s){std::lock_guard guard(logMutex);logFile<<s<<'\n';logFile.flush();}
std::string dimensions(const NVSDK_NGX_Parameter* p){
    std::ostringstream s;s<<"{";bool first=true;
    for(auto name:{NVSDK_NGX_Parameter_Width,NVSDK_NGX_Parameter_Height,NVSDK_NGX_Parameter_OutWidth,NVSDK_NGX_Parameter_OutHeight}){
        unsigned n=0;auto r=p->Get(name,&n);if(!first)s<<',';first=false;s<<'"'<<name<<"\":{\"value\":"<<n<<",\"get_result\":\""<<hex(r)<<"\"}";
    }
    for(auto name:{NVSDK_NGX_Parameter_PerfQualityValue,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags}){
        int n=0;auto r=p->Get(name,&n);s<<",\""<<name<<"\":{\"value\":"<<n<<",\"get_result\":\""<<hex(r)<<"\"}";
    }
    s<<'}';return s.str();
}
void attemptRr(ID3D12GraphicsCommandList* nativeList,const NVSDK_NGX_Parameter* nativeParams,void* caller){
    unsigned iw=0,ih=0,ow=0,oh=0;
    nativeParams->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&iw);
    nativeParams->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&ih);
    nativeParams->Get(NVSDK_NGX_Parameter_OutWidth,&ow);nativeParams->Get(NVSDK_NGX_Parameter_OutHeight,&oh);
    if(!iw||!ih||!ow||!oh)return;
    bool expected=false;if(!attempted.compare_exchange_strong(expected,true))return;
    NVSDK_NGX_Parameter* p=nullptr;
    auto capabilityResult=getCapabilities(&p);
    emit("{\"event\":\"rr_capabilities\",\"call\":\"_nvngx.dll!NVSDK_NGX_D3D12_GetCapabilityParameters\",\"result\":\""+hex(capabilityResult)+"\"}");
    if(NVSDK_NGX_FAILED(capabilityResult)||!p)return;
    int available=-1;auto availableResult=p->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&available);
    emit("{\"event\":\"rr_available\",\"value\":"+std::to_string(available)+",\"result\":\""+hex(availableResult)+"\"}");
    // Use the existing game's initialized NGX session. No second initialization,
    // project-ID change, shutdown, native-parameter mutation or SR replacement.
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    HRESULT hr=nativeList->GetDevice(IID_PPV_ARGS(&device));
    if(SUCCEEDED(hr))hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator));
    if(SUCCEEDED(hr))hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list));
    if(FAILED(hr)){emit("{\"event\":\"rr_list_error\",\"hresult\":\""+hex(unsigned(hr))+"\"}");destroyParameters(p);return;}
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);
    p->Set(NVSDK_NGX_Parameter_Width,iw);p->Set(NVSDK_NGX_Parameter_Height,ih);
    p->Set(NVSDK_NGX_Parameter_OutWidth,ow);p->Set(NVSDK_NGX_Parameter_OutHeight,oh);
    int quality=NVSDK_NGX_PerfQuality_Value_MaxQuality;nativeParams->Get(NVSDK_NGX_Parameter_PerfQualityValue,&quality);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue,quality);
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_IsHDR));
    p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects,0);
    p->Set(NVSDK_NGX_Parameter_DLSS_Denoise_Mode,int(NVSDK_NGX_DLSS_Denoise_Mode_DLUnified));
    p->Set(NVSDK_NGX_Parameter_DLSS_Roughness_Mode,unsigned(NVSDK_NGX_DLSS_Roughness_Mode_Packed));
    p->Set(NVSDK_NGX_Parameter_Use_HW_Depth,unsigned(NVSDK_NGX_DLSS_Depth_Type_Linear));
    for(auto name:{NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
        NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
        NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality})
        p->Set(name,unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F));
    NVSDK_NGX_Handle* handle=nullptr;
    auto result=originalCreate(list.Get(),NVSDK_NGX_Feature_RayReconstruction,p,&handle);
    emit("{\"event\":\"rr_create\",\"call\":\"_nvngx.dll!NVSDK_NGX_D3D12_CreateFeature\",\"trigger_callsite\":\""+site(caller)
        +"\",\"feature\":13,\"input\":["+std::to_string(iw)+","+std::to_string(ih)+"],\"output\":["+std::to_string(ow)+","+std::to_string(oh)
        +"],\"quality\":"+std::to_string(quality)+",\"preset\":\"F\",\"result\":\""+hex(result)+"\",\"handle\":\""+hex(uintptr_t(handle))+"\",\"rr_evaluations\":0,\"output_routed\":false}");
    if(NVSDK_NGX_SUCCEED(result)&&handle){
        // Retain a successful feature and its unsubmitted initialization list.
        // Never release or submit it without the renderer bridge and a fence.
        // This one-shot probe cannot produce a visible RR frame.
        device.Detach();allocator.Detach();list.Detach();
        emit("{\"event\":\"rr_create_retained\",\"gpu_commands_submitted\":false,\"reason\":\"awaiting_same_frame_renderer_bridge\"}");
    }else destroyParameters(p);
}
NVSDK_NGX_Result NVSDK_CONV create(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* parameters,NVSDK_NGX_Handle** handle){
    auto caller=_ReturnAddress();auto result=originalCreate(list,feature,parameters,handle);
    try{emit("{\"event\":\"native_create\",\"callsite\":\""+site(caller)+"\",\"feature\":"+std::to_string(unsigned(feature))
        +",\"parameters\":"+dimensions(parameters)+",\"result\":\""+hex(result)+"\",\"handle\":\""+hex(handle?uintptr_t(*handle):0)+"\"}");}catch(...){}
    return result;
}
NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* parameters,PFN_NVSDK_NGX_ProgressCallback callback){
    auto caller=_ReturnAddress();
    auto bridge=GetModuleHandleW(L"bedrock_rr_bridge.dll");
    if(bridge){auto before=reinterpret_cast<bool(*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Parameter*)>(GetProcAddress(bridge,"BedrockRrBeforeSr"));if(before&&before(list,parameters))return NVSDK_NGX_Result_Success;}
    auto result=originalEvaluate(list,handle,parameters,callback);
    static std::atomic_uint count{0};
    try{
        if(count++<8)emit("{\"event\":\"native_evaluate\",\"callsite\":\""+site(caller)+"\",\"handle\":\""+hex(uintptr_t(handle))
            +"\",\"parameters\":"+dimensions(parameters)+",\"result\":\""+hex(result)+"\"}");
        if(!bridge&&NVSDK_NGX_SUCCEED(result))attemptRr(list,parameters,caller);
    }catch(...){emit("{\"event\":\"activation_exception\",\"rr_evaluations\":0}");}
    return result;
}
bool start(){
    if(GetModuleHandleW(L"bedrock_rr_ngx_observer.dll"))return true;
    if(started)return true;auto ngx=GetModuleHandleW(L"_nvngx.dll");if(!ngx)return false;
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);auto folder=std::filesystem::path(path).parent_path()/L"activation";std::filesystem::create_directories(folder);
    logFile.open(folder/L"events.jsonl",std::ios::trunc);
    auto a=GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"),b=GetProcAddress(ngx,"NVSDK_NGX_D3D12_EvaluateFeature");
    getCapabilities=reinterpret_cast<decltype(getCapabilities)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_GetCapabilityParameters"));
    destroyParameters=reinterpret_cast<decltype(destroyParameters)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));
    releaseFeature=reinterpret_cast<decltype(releaseFeature)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));
    if(!a||!b||!getCapabilities||!destroyParameters){emit("{\"event\":\"ngx_export_missing\",\"win32_error\":127}");return false;}
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(a,reinterpret_cast<void*>(&create),reinterpret_cast<void**>(&originalCreate))!=MH_OK)return false;
    if(MH_CreateHook(b,reinterpret_cast<void*>(&evaluate),reinterpret_cast<void**>(&originalEvaluate))!=MH_OK){MH_RemoveHook(a);return false;}
    MH_QueueEnableHook(a);MH_QueueEnableHook(b);if(MH_ApplyQueued()!=MH_OK){MH_DisableHook(a);MH_DisableHook(b);return false;}
    started=true;emit("{\"event\":\"activation_probe_started\",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"replaces_sr\":false}");return true;
}
DWORD WINAPI worker(void*){wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);if(_wcsicmp(std::filesystem::path(image).filename().c_str(),L"Minecraft.Windows.exe"))return 0;
    for(;;){if(start())return 0;Sleep(1000);}}
}
extern "C" __declspec(dllexport) bool NgxActivationStart(){return start();}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
