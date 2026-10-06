#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs_dlssd.h"
namespace {
HMODULE module;
std::mutex mutex;
std::ofstream file;
bool started=false;
std::unordered_map<const NVSDK_NGX_Handle*,unsigned> features,samples;
std::unordered_set<const NVSDK_NGX_Handle*> srNeedsReset;
decltype(&NVSDK_NGX_D3D12_CreateFeature) realCreate=nullptr;
decltype(&NVSDK_NGX_D3D12_EvaluateFeature) realEvaluate=nullptr;
std::string hex(uint64_t v){std::ostringstream s;s<<std::hex<<v;return s.str();}
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> ngxList(ID3D12GraphicsCommandList* list){
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> native;
    auto bridge=GetModuleHandleW(L"bedrock_rr_bridge.dll");
    auto required=bridge?reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(bridge,"BedrockFgNativeNgxLists")):nullptr;
    constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    if(list&&required&&required())list->QueryInterface(unwrapped,reinterpret_cast<void**>(native.GetAddressOf()));
    if(!native)native=list;return native;
}
std::string snapshot(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* parameters){
    std::ostringstream s;
    s<<"{\"event\":\"ngx_evaluate\",\"handle\":\""<<hex(uintptr_t(handle))<<"\",\"feature\":";
    auto it=features.find(handle);if(it!=features.end())s<<it->second;else s<<"null";
    s<<",\"list\":\""<<hex(uintptr_t(list))<<"\",\"textures\":[";bool first=true;
    for(const char* name:{NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Output,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,
        NVSDK_NGX_Parameter_DiffuseAlbedo,NVSDK_NGX_Parameter_SpecularAlbedo,NVSDK_NGX_Parameter_GBuffer_Normals,
        NVSDK_NGX_Parameter_GBuffer_Roughness,NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance}) {
        ID3D12Resource* r=nullptr;if(NVSDK_NGX_FAILED(parameters->Get(name,&r))||!r)continue;
        auto d=r->GetDesc();if(!first)s<<',';first=false;
        s<<"{\"parameter\":\""<<name<<"\",\"resource\":\""<<hex(uintptr_t(r))<<"\",\"width\":"<<d.Width<<",\"height\":"<<d.Height<<",\"format\":"<<unsigned(d.Format)<<'}';
    }
    s<<"],\"floats\":{";first=true;
    for(const char* name:{NVSDK_NGX_Parameter_Jitter_Offset_X,NVSDK_NGX_Parameter_Jitter_Offset_Y,NVSDK_NGX_Parameter_MV_Scale_X,
        NVSDK_NGX_Parameter_MV_Scale_Y,NVSDK_NGX_Parameter_DLSS_Pre_Exposure,NVSDK_NGX_Parameter_DLSS_Exposure_Scale,NVSDK_NGX_Parameter_FrameTimeDeltaInMsec}){
        float value=0;if(NVSDK_NGX_FAILED(parameters->Get(name,&value))||!std::isfinite(value))continue;if(!first)s<<',';first=false;s<<'"'<<name<<"\":"<<value;
    }
    s<<"},\"integers\":{";first=true;
    for(const char* name:{NVSDK_NGX_Parameter_Width,NVSDK_NGX_Parameter_Height,NVSDK_NGX_Parameter_OutWidth,NVSDK_NGX_Parameter_OutHeight,
        NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,NVSDK_NGX_Parameter_Reset}){
        unsigned value=0;if(NVSDK_NGX_FAILED(parameters->Get(name,&value)))continue;if(!first)s<<',';first=false;s<<'"'<<name<<"\":"<<value;
    }
    s<<"}}";return s.str();
}
NVSDK_NGX_Result NVSDK_CONV create(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* parameters,NVSDK_NGX_Handle** handle){
    auto native=ngxList(list);auto result=realCreate(native.Get(),feature,parameters,handle);
    if(NVSDK_NGX_SUCCEED(result)&&handle&&*handle)try{std::lock_guard guard(mutex);features[*handle]=unsigned(feature);file<<"{\"event\":\"ngx_create\",\"feature\":"<<unsigned(feature)<<",\"handle\":\""<<hex(uintptr_t(*handle))<<"\"}\n";file.flush();}catch(...){}
    return result;
}
NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* parameters,PFN_NVSDK_NGX_ProgressCallback callback){
    // This module owns the NGX evaluation hook. Other modules must not race
    // to patch the same entry point with independent MinHook instances.
    auto bridge=GetModuleHandleW(L"bedrock_rr_bridge.dll");
    bool isSr=false,isFg=false;
    {std::lock_guard guard(mutex);auto it=features.find(handle);isSr=it!=features.end()&&it->second==unsigned(NVSDK_NGX_Feature_SuperSampling);isFg=it!=features.end()&&it->second==11;}
    if(bridge&&isSr){auto before=reinterpret_cast<bool(*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Parameter*)>(GetProcAddress(bridge,"BedrockRrBeforeSr"));
        if(before&&before(list,parameters)){
            {std::lock_guard guard(mutex);srNeedsReset.insert(handle);}
            static unsigned bypasses=0;
            if(bypasses++<16){std::lock_guard guard(mutex);file<<"{\"event\":\"native_sr_bypassed\",\"native_sr_executed\":false,\"rr_output_complete\":true,\"result\":\"0x1\"}\n";file.flush();}
            return NVSDK_NGX_Result_Success;
        }
    }
    if(list&&parameters)try{std::lock_guard guard(mutex);if(samples[handle]++<16){file<<snapshot(list,handle,parameters)<<'\n';file.flush();}}catch(...){}
    bool resetSr=false;
    if(isSr){std::lock_guard guard(mutex);resetSr=srNeedsReset.erase(handle)!=0;}
    unsigned savedReset=0;
    auto mutableParameters=const_cast<NVSDK_NGX_Parameter*>(parameters);
    if(resetSr&&parameters){parameters->Get(NVSDK_NGX_Parameter_Reset,&savedReset);mutableParameters->Set(NVSDK_NGX_Parameter_Reset,1u);}
    if(bridge&&isFg){auto observe=reinterpret_cast<void(*)(const NVSDK_NGX_Parameter*)>(GetProcAddress(bridge,"BedrockFgObserveEvaluate"));if(observe)observe(parameters);}
    auto native=ngxList(list);auto result=realEvaluate(native.Get(),handle,parameters,callback);
    if(resetSr&&parameters){mutableParameters->Set(NVSDK_NGX_Parameter_Reset,savedReset);std::lock_guard guard(mutex);file<<"{\"event\":\"native_sr_resumed\",\"history_reset\":true,\"result\":\"0x"<<hex(unsigned(result))<<"\"}\n";file.flush();}
    return result;
}
bool start(){
    std::lock_guard guard(mutex);if(started)return true;
    auto ngx=GetModuleHandleW(L"_nvngx.dll");if(!ngx)return false;
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
    auto folder=std::filesystem::path(path).parent_path()/L"ngx-capture";std::filesystem::create_directories(folder);
    file.open(folder/L"events.jsonl",std::ios::trunc);
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    auto a=GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"),b=GetProcAddress(ngx,"NVSDK_NGX_D3D12_EvaluateFeature");
    if(!a||!b)return false;
    if(MH_CreateHook(a,reinterpret_cast<void*>(&create),reinterpret_cast<void**>(&realCreate))!=MH_OK)return false;
    if(MH_CreateHook(b,reinterpret_cast<void*>(&evaluate),reinterpret_cast<void**>(&realEvaluate))!=MH_OK){MH_RemoveHook(a);return false;}
    MH_QueueEnableHook(a);MH_QueueEnableHook(b);
    if(MH_ApplyQueued()!=MH_OK){MH_DisableHook(a);MH_DisableHook(b);return false;}
    started=true;file<<"{\"event\":\"ngx_capture_started\",\"mode\":\"observe-only\"}\n";file.flush();return true;
}
DWORD WINAPI worker(void*){
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    if(_wcsicmp(std::filesystem::path(image).filename().c_str(),L"Minecraft.Windows.exe"))return 0;
    // The player may spend several minutes at the menu before enabling RTX.
    // Keep waiting until NGX appears; observation never changes its inputs.
    for(;;){if(start())return 0;Sleep(1000);}
}
}
extern "C" __declspec(dllexport) bool NgxObserverStart(){return start();}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);HANDLE thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
