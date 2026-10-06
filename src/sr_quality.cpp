#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <cstring>
#include "sr_quality.h"
#include "resolution_modes.h"
namespace {
std::atomic_int selected{-1};
std::atomic<float> customScale{.75f};
std::atomic<unsigned> configurationRevision{0};
PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback originalOptimal=nullptr;
using NativeSettings=int(*)(void*,const unsigned*);
NativeSettings originalNativeSettings=nullptr;
using FrontConfigure=void(*)(void*,bool,const void*,const void*,float,void*,void*);
FrontConfigure originalFrontConfigure=nullptr;
std::ofstream log;std::mutex logMutex;
std::string hex(uint64_t n){std::ostringstream s;s<<"0x"<<std::hex<<n;return s.str();}
void emit(const std::string& s){std::lock_guard guard(logMutex);if(log){log<<s<<'\n';log.flush();}}
const char* name(int q){switch(q){case -1:return "vanilla";case cw_resolution::FraudulentDlaa:return "Fraudulent DLAA";case cw_resolution::Caca:return "Caca 144p";case cw_resolution::Custom:return "Custom";case NVSDK_NGX_PerfQuality_Value_DLAA:return "DLAA";case NVSDK_NGX_PerfQuality_Value_MaxQuality:return "Quality";case NVSDK_NGX_PerfQuality_Value_Balanced:return "Balanced";case NVSDK_NGX_PerfQuality_Value_MaxPerf:return "Performance";case NVSDK_NGX_PerfQuality_Value_UltraPerformance:return "UltraPerformance";default:return "invalid";}}
bool choose(int q){
    if(!cw_resolution::valid(q))return false;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    if(q>=0&&!_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"Minecraft.Windows.exe")&&
       !originalFrontConfigure){
        // The live renderer allocates its ray-tracing textures before this
        // module's NGX override. Separate NGX GPU tests cannot validate that
        // allocation path. Reject overrides until the upstream bridge exists.
        emit(std::string("{\"event\":\"sr_quality_rejected\",\"requested\":\"")+name(q)+"\",\"reason\":\"Bedrock upstream lighting allocations are not synchronized with the requested SR input\",\"active\":\"vanilla\",\"resize_requested\":false}");
        return false;
    }
    selected=q;configurationRevision.fetch_add(1);emit("{\"event\":\"sr_quality_selected\",\"quality\":"+std::to_string(q)+",\"name\":\""+name(q)+"\"}");return true;
}
int nativeSettings(void* object,const unsigned* display){
    // Once the frontend owns quality and render scale, let its submitted
    // configuration drive the backend. A late backend-only override causes
    // the lighting allocations and camera/NGX dimensions to disagree.
    if(originalFrontConfigure){
        // Bedrock compares only cached/requested NGX quality (0xdfc6e2f),
        // not our custom scale. Invalidate that cache once per selection so
        // its original teardown/query/create path owns the whole transition.
        static std::unordered_map<void*,unsigned> revisions;
        auto revision=configurationRevision.load();auto& applied=revisions[object];
        if(revision!=applied){
            auto current=reinterpret_cast<int*>(static_cast<char*>(object)+0x3c56418);
            MEMORY_BASIC_INFORMATION memory{};
            if(VirtualQuery(current,&memory,sizeof(memory))&&memory.State==MEM_COMMIT&&!(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&uintptr_t(current)+4<=uintptr_t(memory.BaseAddress)+memory.RegionSize){
                *current=-1;applied=revision;
                emit("{\"event\":\"native_resolution_cache_invalidated\",\"revision\":"+std::to_string(revision)+",\"current_quality_offset\":\"0x3c56418\",\"native_recreation_path\":true}");
            }
        }
        return originalNativeSettings(object,display);
    }
    auto requested=reinterpret_cast<int*>(static_cast<char*>(object)+0x3c56428);
    MEMORY_BASIC_INFORMATION memory{};bool valid=VirtualQuery(requested,&memory,sizeof(memory))&&memory.State==MEM_COMMIT&&!(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&uintptr_t(requested)+4<=uintptr_t(memory.BaseAddress)+memory.RegionSize;
    struct Saved {int quality;bool overridden;};static std::unordered_map<void*,Saved> saved;
    if(valid&&*requested>=0&&*requested<=5){auto q=selected.load();auto found=saved.find(object);if(found==saved.end())found=saved.emplace(object,Saved{*requested,false}).first;
        if(q>=0){if(!found->second.overridden)found->second.quality=*requested;*requested=cw_resolution::quality(q);found->second.overridden=true;}
        else if(found->second.overridden){*requested=found->second.quality;found->second.overridden=false;}
        else found->second.quality=*requested;
    }
    return originalNativeSettings(object,display);
}
void frontConfigure(void* object,bool on,const void* options,const void* screen,float scale,void* callback,void* debug){
    auto q=selected.load();
    if(on&&options&&screen&&q>=0&&static_cast<const int*>(options)[0]==0){
        const auto dimensions=static_cast<const unsigned short*>(screen);
        const float requestedScale=cw_resolution::scale(q,dimensions[1],customScale.load());
        const auto nativeQuality=cw_resolution::quality(q);
        unsigned localOptions[4];memcpy(localOptions,options,sizeof(localOptions));localOptions[1]=unsigned(nativeQuality);
        static int lastMode=-2;static unsigned lastW=0,lastH=0;static float lastScale=0;
        if(q!=lastMode||dimensions[0]!=lastW||dimensions[1]!=lastH||requestedScale!=lastScale){
            lastMode=q;lastW=dimensions[0];lastH=dimensions[1];lastScale=requestedScale;
            emit("{\"event\":\"frontend_quality_config\",\"function_rva\":\"0xbcd1df0\",\"native_scale\":"+std::to_string(scale)+",\"requested_scale\":"+std::to_string(requestedScale)+",\"mode\":"+std::to_string(q)+",\"requested_quality\":"+std::to_string(nativeQuality)+",\"display\":["+std::to_string(lastW)+","+std::to_string(lastH)+"],\"uses_native_resolution_callback\":true}");
        }
        originalFrontConfigure(object,on,localOptions,screen,requestedScale,callback,debug);
    }else originalFrontConfigure(object,on,options,screen,scale,callback,debug);
}
bool hookFrontConfigure(){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);if(_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"Minecraft.Windows.exe"))return true;
    auto target=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr))+0xbcd1df0;
    const unsigned char signature[]={0x55,0x41,0x56,0x56,0x57,0x53,0x48,0x83,0xec,0x40,0x48,0x8d,0x6c,0x24,0x40};
    MEMORY_BASIC_INFORMATION memory{};if(!VirtualQuery(target,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||memcmp(target,signature,sizeof(signature))){emit("{\"event\":\"frontend_quality_signature_mismatch\",\"quality_enabled\":false}");return false;}
    auto status=MH_CreateHook(target,reinterpret_cast<void*>(&frontConfigure),reinterpret_cast<void**>(&originalFrontConfigure));
    if(status!=MH_OK){originalFrontConfigure=nullptr;return false;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalFrontConfigure=nullptr;return false;}
    emit("{\"event\":\"frontend_quality_path_ready\",\"function_rva\":\"0xbcd1df0\",\"signature_verified\":true,\"live_quality_verified\":false}");return true;
}
bool hookNativeSettings(){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);if(_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"Minecraft.Windows.exe"))return true;
    auto target=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr))+0xdfc6d40;
    const unsigned char prologue[]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xec,0x68};
    MEMORY_BASIC_INFORMATION memory{};if(!VirtualQuery(target,&memory,sizeof(memory))||memory.State!=MEM_COMMIT||memcmp(target,prologue,sizeof(prologue))){emit("{\"event\":\"native_quality_signature_mismatch\",\"controls_enabled\":false}");return false;}
    if(MH_CreateHook(target,reinterpret_cast<void*>(&nativeSettings),reinterpret_cast<void**>(&originalNativeSettings))!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalNativeSettings=nullptr;return false;}
    emit("{\"event\":\"native_quality_path_ready\",\"function_rva\":\"0xdfc6d40\",\"optimal_query_return_rva\":\"0xdfc6f52\",\"requested_quality_offset\":\"0x3c56428\",\"current_quality_offset\":\"0x3c56418\",\"signature_verified\":true}");return true;
}
NVSDK_NGX_Result NVSDK_CONV optimal(NVSDK_NGX_Parameter* p){
    auto caller=_ReturnAddress();int nativeQuality=-1;p->Get(NVSDK_NGX_Parameter_PerfQualityValue,&nativeQuality);int q=selected.load();
    if(q>=0&&!originalNativeSettings)p->Set(NVSDK_NGX_Parameter_PerfQualityValue,cw_resolution::quality(q));
    auto result=originalOptimal(p);
    unsigned displayW=0,displayH=0,renderW=0,renderH=0;p->Get(NVSDK_NGX_Parameter_Width,&displayW);p->Get(NVSDK_NGX_Parameter_Height,&displayH);p->Get(NVSDK_NGX_Parameter_OutWidth,&renderW);p->Get(NVSDK_NGX_Parameter_OutHeight,&renderH);
    if(!NVSDK_NGX_FAILED(result)&&q>=100&&displayW>1&&displayH>1){
        const auto size=cw_resolution::dimensions(q,displayW,displayH,customScale.load());
        renderW=size.width;renderH=size.height;
        p->Set(NVSDK_NGX_Parameter_OutWidth,renderW);p->Set(NVSDK_NGX_Parameter_OutHeight,renderH);
        p->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width,renderW);p->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width,renderW);
        p->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height,renderH);p->Set(NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height,renderH);
    }
    // Bedrock reads the native provider's size results to allocate its actual
    // ray-tracing inputs. No resampling or invented DLAA is used here.
    if(q>=0&&!originalNativeSettings)p->Set(NVSDK_NGX_Parameter_PerfQualityValue,nativeQuality);
    static unsigned vanillaLogs=0;if(q>=0||vanillaLogs++<8){HMODULE owner=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(caller),&owner);
        wchar_t path[32768]{};GetModuleFileNameW(owner,path,32768);emit("{\"event\":\"sr_optimal_settings\",\"caller\":\""+std::filesystem::path(path).filename().string()+"+"+hex(uintptr_t(caller)-uintptr_t(owner))+"\",\"native_quality\":"+std::to_string(nativeQuality)+",\"selected\":\""+name(q)+"\",\"display\":["+std::to_string(displayW)+","+std::to_string(displayH)+"],\"render\":["+std::to_string(renderW)+","+std::to_string(renderH)+"],\"result\":\""+hex(unsigned(result))+"\"}");}
    return result;
}
bool initialize(){
    static std::mutex startupMutex;std::lock_guard startupGuard(startupMutex);
    if(originalOptimal)return true;auto ngx=GetModuleHandleW(L"_nvngx.dll");if(!ngx)return false;
    auto caps=reinterpret_cast<decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_GetCapabilityParameters"));auto destroy=reinterpret_cast<decltype(&NVSDK_NGX_D3D12_DestroyParameters)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));if(!caps||!destroy)return false;
    NVSDK_NGX_Parameter* p=nullptr;auto r=caps(&p);if(NVSDK_NGX_FAILED(r)||!p)return false;void* callback=nullptr;p->Get(NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback,&callback);destroy(p);if(!callback)return false;
    if(!log.is_open()){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto folder=std::filesystem::path(exe).parent_path()/L"quality-controls";std::filesystem::create_directories(folder);log.open(folder/L"events.jsonl",std::ios::trunc);}
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(!originalNativeSettings&&!hookNativeSettings())return false;
    if(!originalFrontConfigure&&!hookFrontConfigure())return false;
    auto status=MH_CreateHook(callback,reinterpret_cast<void*>(&optimal),reinterpret_cast<void**>(&originalOptimal));if(status!=MH_OK){static unsigned errors=0;if(errors++<4)emit("{\"event\":\"optimal_hook_error\",\"status\":"+std::to_string(status)+",\"callback\":\""+hex(uintptr_t(callback))+"\"}");originalOptimal=nullptr;return false;}
    status=MH_EnableHook(callback);if(status!=MH_OK){emit("{\"event\":\"optimal_enable_error\",\"status\":"+std::to_string(status)+"}");MH_RemoveHook(callback);originalOptimal=nullptr;return false;}
    emit("{\"event\":\"sr_quality_ready\",\"callback\":\""+hex(uintptr_t(callback))+"\",\"initial_quality\":\"vanilla\",\"minecraft_custom_modes_validated\":false,\"keys\":\"F8 menu\"}");return true;
}
}
void srQualityBeforeCreate(NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* p,int& originalQuality,bool& changed){changed=false;if(feature!=NVSDK_NGX_Feature_SuperSampling)return;auto q=selected.load();if(q<0)return;
    if(NVSDK_NGX_FAILED(p->Get(NVSDK_NGX_Parameter_PerfQualityValue,&originalQuality)))return;p->Set(NVSDK_NGX_Parameter_PerfQualityValue,cw_resolution::quality(q));changed=true;
}
void srQualityAfterCreate(NVSDK_NGX_Parameter* p,int originalQuality,bool changed){if(changed)p->Set(NVSDK_NGX_Parameter_PerfQualityValue,originalQuality);}
void srQualityPoll(){initialize();}
// Also exercised by the standalone NGX GPU verification program.
extern "C" __declspec(dllexport) bool NgxQualityStart(){return initialize();}
extern "C" __declspec(dllexport) bool NgxQualitySelect(int quality){return choose(quality);}


extern "C" __declspec(dllexport) int NgxQualityGet(){return selected.load();}
extern "C" __declspec(dllexport) float NgxRenderScaleGet(){return customScale.load();}
extern "C" __declspec(dllexport) bool NgxRenderScaleSelect(float ratio){if(!std::isfinite(ratio)||ratio<.05f||ratio>.999f)return false;customScale.store(ratio);return choose(cw_resolution::Custom);}
