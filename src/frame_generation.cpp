#include "frame_generation.h"
#include <windows.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <DirectXMath.h>
#include <imgui.h>
#include <reshade.hpp>
#include <array>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include "nvsdk_ngx.h"
#include "nvapi.h"
#include "device_identity.h"
namespace fg {NVSDK_NGX_Result NVSDK_CONV create(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);}
#define NVSDK_NGX_D3D12_CreateFeature fg::create
#define NVSDK_NGX_D3D12_EvaluateFeature_C fg::evaluate
#define NVSDK_NGX_Parameter_SetUI(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetF(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetVoidPointer(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetD3d12Resource(p,n,v) (p)->Set(n,v)
#include "nvsdk_ngx_helpers_dlssg_d3d.h"
#undef NVSDK_NGX_D3D12_CreateFeature
using Microsoft::WRL::ComPtr;
namespace fg {
std::mutex traceMutex;std::ofstream deliveryTrace;
std::mutex mutex;thread_local bool recording=false;
bool paused=false;
bool reflexEnabled=false;unsigned sleepCalls=0,reflexInterval=0,cadence=0;float refreshHz=0;bool pacedFrame=false;HMONITOR refreshMonitor=nullptr;
unsigned requested=0,maximum=1,evaluations=0,presented=0,lastResult=0,frame=0,lastFrame=0;
const char* status="Off";std::ofstream log;
ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
// Frame copies stay queued on the native presentation queue. Allocators are
// reset only after their own completion fence, rather than waiting per copy.
struct Commands{ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;UINT64 fence=0;};std::array<Commands,8> commandPool;unsigned commandIndex=0;
ComPtr<ID3D12Fence> completion,producerFence;UINT64 tick=0,producerTick=0;HANDLE event=nullptr;
ID3D12GraphicsCommandList* producer=nullptr;IDXGISwapChain3* chain=nullptr;
ComPtr<ID3D12Resource> depth,motion,real,realOutput,disable,disableRead;
std::array<ComPtr<ID3D12Resource>,5> interpolated;
ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pipeline;ComPtr<ID3D12DescriptorHeap> heap;
NVSDK_NGX_Parameter* params=nullptr;NVSDK_NGX_Handle* feature=nullptr;
decltype(&NVSDK_NGX_D3D12_CreateFeature) createApi=nullptr;
decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluateApi=nullptr;
decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters) capabilities=nullptr;
decltype(&NVSDK_NGX_D3D12_DestroyParameters) destroyParams=nullptr;
decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release=nullptr;
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
Present originalPresent=nullptr;Present1 originalPresent1=nullptr;
std::array<unsigned char,1792> camera{};unsigned iw=0,ih=0,ow=0,oh=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
std::chrono::steady_clock::time_point lastRealTime{},sampleBegin{};float renderedFps=0,presentedFps=0;unsigned sampleReal=0,sampleDisplayed=0;
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 frame-generation operation failed");}
void report(const char* call,unsigned result,unsigned index=0){lastResult=result;log<<"{\"call\":\""<<call<<"\",\"result\":\"0x"<<std::hex<<result<<std::dec<<"\",\"frame\":"<<frame<<",\"index\":"<<index<<",\"generated_presentations\":"<<presented
#ifdef FG_TEST
    <<",\"source\":\"synthetic_swapchain_test\""
#else
    <<",\"source\":\"Minecraft.Windows.exe\""
#endif
    <<"}\n";log.flush();}
void rates(unsigned generatedBefore){std::lock_guard guard(mutex);auto now=std::chrono::steady_clock::now();if(sampleBegin.time_since_epoch().count()==0)sampleBegin=now;sampleReal++;sampleDisplayed+=1+presented-generatedBefore;auto seconds=std::chrono::duration<float>(now-sampleBegin).count();if(seconds>=1){renderedFps=sampleReal/seconds;presentedFps=sampleDisplayed/seconds;log<<"{\"event\":\"presentation_rates\",\"fg_active\":"<<(requested&&!paused?"true":"false")<<",\"rendered_fps\":"<<renderedFps<<",\"submitted_fps\":"<<presentedFps<<",\"refresh_hz\":"<<refreshHz<<",\"refresh_divisor\":"<<(pacedFrame?cadence:0)<<",\"target_submitted_fps\":"<<(pacedFrame&&cadence?refreshHz/cadence:0)<<"}\n";log.flush();sampleReal=sampleDisplayed=0;sampleBegin=now;}}
void reflex(bool enabled){unsigned interval=enabled&&pacedFrame&&cadence&&refreshHz>0?unsigned(1000000.0*(std::min(requested,maximum)+1)*cadence/refreshHz):0;if(!device||(enabled==reflexEnabled&&interval==reflexInterval))return;auto init=NvAPI_Initialize();report("NvAPI_Initialize",unsigned(init));if(init!=NVAPI_OK)return;auto native=bedrock_rr::deviceIdentity(device.Get());NV_SET_SLEEP_MODE_PARAMS p{};p.version=NV_SET_SLEEP_MODE_PARAMS_VER;p.bLowLatencyMode=enabled;p.bUseMarkersToOptimize=false;p.minimumIntervalUs=interval;auto r=NvAPI_D3D_SetSleepMode(native.Get(),&p);report(enabled?"NvAPI_D3D_SetSleepMode(on)":"NvAPI_D3D_SetSleepMode(off)",unsigned(r));if(r==NVAPI_OK){reflexEnabled=enabled;reflexInterval=interval;report("Reflex minimumIntervalUs",0,interval);}NV_GET_SLEEP_STATUS_PARAMS s{};s.version=NV_GET_SLEEP_STATUS_PARAMS_VER;auto q=NvAPI_D3D_GetSleepStatus(native.Get(),&s);report("NvAPI_D3D_GetSleepStatus",unsigned(q),s.bLowLatencyMode);}
void sleep(){std::lock_guard guard(mutex);reflex(requested&&!paused);if(!reflexEnabled||!requested||paused)return;auto native=bedrock_rr::deviceIdentity(device.Get());auto r=NvAPI_D3D_Sleep(native.Get());if(sleepCalls++<8||r!=NVAPI_OK){report("NvAPI_D3D_Sleep(next real-frame boundary)",unsigned(r));NV_GET_SLEEP_STATUS_PARAMS s{};s.version=NV_GET_SLEEP_STATUS_PARAMS_VER;auto q=NvAPI_D3D_GetSleepStatus(native.Get(),&s);report("NvAPI_D3D_GetSleepStatus(after sleep)",unsigned(q),s.bLowLatencyMode);}}
NVSDK_NGX_Result NVSDK_CONV create(ID3D12GraphicsCommandList* l,NVSDK_NGX_Feature f,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h){return createApi(l,f,p,h);}
NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList* l,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback c){return evaluateApi(l,h,p,c);}
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);}
ComPtr<ID3D12Resource> texture(unsigned w,unsigned h,DXGI_FORMAT f,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=f;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;}
ComPtr<ID3D12Resource> buffer(D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=256;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(type==D3D12_HEAP_TYPE_DEFAULT)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;}
void waitGpu(UINT64 value){if(completion->GetCompletedValue()>=value)return;check(completion->SetEventOnCompletion(value,event));if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("FG GPU timeout");}
void submit(bool wait=true){check(list->Close());constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};ComPtr<IUnknown> original;ComPtr<ID3D12CommandList> native;ID3D12CommandList* command=list.Get();if(SUCCEEDED(list->QueryInterface(unwrapped,reinterpret_cast<void**>(original.GetAddressOf())))){check(original.As(&native));command=native.Get();}ID3D12CommandList* l[]={command};queue->ExecuteCommandLists(1,l);check(queue->Signal(completion.Get(),++tick));commandPool[commandIndex].fence=tick;if(wait)waitGpu(tick);check(device->GetDeviceRemovedReason());commandIndex=(commandIndex+1)%commandPool.size();auto& next=commandPool[commandIndex];if(next.fence){waitGpu(next.fence);check(next.allocator->Reset());check(next.list->Reset(next.allocator.Get(),nullptr));}allocator=next.allocator;list=next.list;}
void retire(){if(completion&&tick&&device&&SUCCEEDED(device->GetDeviceRemovedReason()))waitGpu(tick);if(feature&&release)release(feature);feature=nullptr;if(params&&destroyParams)destroyParams(params);params=nullptr;real.Reset();realOutput.Reset();for(auto& r:interpolated)r.Reset();ow=oh=0;lastFrame=0;}
void setupDepth(){
    const char* shader=R"(Texture2D<float> source:register(t0);RWTexture2D<float> dest:register(u0);cbuffer C:register(b0){float A,B,C,D;uint W,H;}[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){if(p.x>=W||p.y>=H)return;float z=sign(C)*source[p.xy];dest[p.xy]=saturate((A*z+B)/(C*z+D));})";
    ComPtr<ID3DBlob> code,errors;check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"main","cs_5_1",0,0,&code,&errors));
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
    D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,6};
    D3D12_ROOT_SIGNATURE_DESC desc{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob;check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.Get();ps.CS={code->GetBufferPointer(),code->GetBufferSize()};check(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline)));
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
}
void capture(ID3D12GraphicsCommandList* l,ID3D12Resource* z,ID3D12Resource* mv,const unsigned char* cb,unsigned w,unsigned h,unsigned f){
    std::lock_guard guard(mutex);if(!requested)return;
    if(!device){check(l->GetDevice(IID_PPV_ARGS(&device)));setupDepth();check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&producerFence)));reflex(true);}
    if(w!=iw||h!=ih){if(producer&&(!producerTick||producerFence->GetCompletedValue()<producerTick))return;retire();depth=texture(w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);motion=texture(w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);iw=w;ih=h;}
    memcpy(camera.data(),cb,1792);frame=f;producer=l;producerTick=0;
    auto handle=heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=DXGI_FORMAT_R32_FLOAT;sr.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sr.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sr.Texture2D.MipLevels=1;device->CreateShaderResourceView(z,&sr,handle);
    handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_UNORDERED_ACCESS_VIEW_DESC ua{};ua.Format=DXGI_FORMAT_R32_FLOAT;ua.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(depth.Get(),nullptr,&ua,handle);
    barrier(l,z,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    struct Constants{float a,b,c,d;unsigned w,h;} constants{};float projection[16];memcpy(projection,cb+128,64);constants={projection[10],projection[14],projection[11],projection[15],w,h};
    ID3D12DescriptorHeap* heaps[]={heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pipeline.Get());l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,6,&constants,0);l->Dispatch((w+7)/8,(h+7)/8,1);
    barrier(l,z,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    barrier(l,mv,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(l,motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);l->CopyResource(motion.Get(),mv);barrier(l,motion.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(l,mv,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
bool runtime(){auto dll=GetModuleHandleW(L"_nvngx.dll");if(!dll)return false;
    createApi=reinterpret_cast<decltype(createApi)>(GetProcAddress(dll,"NVSDK_NGX_D3D12_CreateFeature"));evaluateApi=reinterpret_cast<decltype(evaluateApi)>(GetProcAddress(dll,"NVSDK_NGX_D3D12_EvaluateFeature"));capabilities=reinterpret_cast<decltype(capabilities)>(GetProcAddress(dll,"NVSDK_NGX_D3D12_GetCapabilityParameters"));destroyParams=reinterpret_cast<decltype(destroyParams)>(GetProcAddress(dll,"NVSDK_NGX_D3D12_DestroyParameters"));release=reinterpret_cast<decltype(release)>(GetProcAddress(dll,"NVSDK_NGX_D3D12_ReleaseFeature"));return createApi&&evaluateApi&&capabilities&&destroyParams&&release;}
void displayCadence(IDXGISwapChain3* s){
    ComPtr<IDXGIOutput> output;DXGI_OUTPUT_DESC d{};
    if(FAILED(s->GetContainingOutput(&output))||FAILED(output->GetDesc(&d))){refreshHz=0;return;}
    if(refreshMonitor==d.Monitor&&refreshHz>0)return;
    DEVMODEW mode{};mode.dmSize=sizeof(mode);
    if(EnumDisplaySettingsW(d.DeviceName,ENUM_CURRENT_SETTINGS,&mode)&&mode.dmDisplayFrequency>1){refreshMonitor=d.Monitor;refreshHz=float(mode.dmDisplayFrequency);report("Display refresh Hz",0,mode.dmDisplayFrequency);}
    else refreshHz=0;
}
bool prepare(IDXGISwapChain3* s){
    displayCadence(s);
    DXGI_SWAP_CHAIN_DESC1 desc{};check(s->GetDesc1(&desc));
    if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&desc.Format!=DXGI_FORMAT_R10G10B10A2_UNORM){status="Unsupported backbuffer format";return false;}
    if(feature&&(ow!=desc.Width||oh!=desc.Height||format!=desc.Format))retire();
    if(!list){check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion)));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);commandPool[0].allocator=allocator;commandPool[0].list=list;for(unsigned i=1;i<commandPool.size();i++){check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&commandPool[i].allocator)));check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,commandPool[i].allocator.Get(),nullptr,IID_PPV_ARGS(&commandPool[i].list)));}}
    if(!feature){if(!runtime())return false;auto r=capabilities(&params);report("GetCapabilityParameters",unsigned(r));if(!params)return false;int available=0;auto qr=params->Get(NVSDK_NGX_Parameter_FrameGeneration_Available,&available);report("FrameGeneration.Available",unsigned(qr),available);unsigned max=1;params->Get(NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax,&max);maximum=std::clamp(max,1u,5u);if(!available){status="Frame Generation unavailable in Minecraft NGX session";return false;}
        ow=desc.Width;oh=desc.Height;format=desc.Format;real=texture(ow,oh,format,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);realOutput=texture(ow,oh,format,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);for(auto& out:interpolated)out=texture(ow,oh,format,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);disable=buffer(D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);disableRead=buffer(D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        NVSDK_NGX_DLSSG_Create_Params p{ow,oh,unsigned(format),iw,ih,false};r=NGX_D3D12_CREATE_DLSSG(list.Get(),1,1,&feature,params,&p);report("NGX_D3D12_CREATE_DLSSG(feature=11)",unsigned(r));submit();if(NVSDK_NGX_FAILED(r)){status="Frame Generation creation failed (see fg/events.jsonl)";return false;}}
    return true;
}
float value(unsigned off){float v;memcpy(&v,camera.data()+off,4);return v;}
NVSDK_NGX_DLSSG_Opt_Eval_Params options(){
    using namespace DirectX;NVSDK_NGX_DLSSG_Opt_Eval_Params o{};
    auto matrix=[&](unsigned offset){XMFLOAT4X4 m;memcpy(&m,camera.data()+offset,64);return XMLoadFloat4x4(&m);};
    auto save=[](float target[4][4],FXMMATRIX m){XMFLOAT4X4 v;XMStoreFloat4x4(&v,m);memcpy(target,&v,64);};
    save(o.cameraViewToClip,matrix(128));save(o.clipToCameraView,matrix(192));save(o.clipToLensClip,XMMatrixIdentity());
    // Bedrock's camera constants are row-major, row-vector transforms.
    auto currentToPrevious=XMMatrixMultiply(matrix(320),matrix(384));save(o.clipToPrevClip,currentToPrevious);save(o.prevClipToClip,XMMatrixInverse(nullptr,currentToPrevious));
    o.jitterOffset[0]=-value(1304);o.jitterOffset[1]=-value(1308);o.mvecScale[0]=o.mvecScale[1]=1;
    float inv[16];memcpy(inv,camera.data()+256,64);for(unsigned i=0;i<3;i++){o.cameraRight[i]=inv[i];o.cameraUp[i]=inv[4+i];o.cameraFwd[i]=inv[8+i];o.cameraPos[i]=inv[12+i];}
    float p[16];memcpy(p,camera.data()+128,64);float plane0=std::abs(p[14]/p[10]),plane1=std::abs(p[14]/(p[10]-p[11]));o.cameraNear=std::min(plane0,plane1);o.cameraFar=std::max(plane0,plane1);o.cameraFOV=2*atanf(1/std::abs(p[5]));o.cameraAspectRatio=float(ow)/oh;o.depthInverted=plane1<plane0;for(auto& x:o.cameraFwd)x*=p[11]<0?-1.f:1.f;o.cameraMotionIncluded=true;o.colorBuffersHDR=format==DXGI_FORMAT_R16G16B16A16_FLOAT;o.reset=!lastFrame||frame!=lastFrame+1;o.menuDetectionEnabled=true;
    o.mvecsSubrectSize=o.depthSubrectSize={iw,ih};o.backbufferSubrectSize=o.outputInterpSubrectSize=o.outputRealSubrectSize={ow,oh};return o;
}
void copyFrame(ID3D12Resource* from,IDXGISwapChain3* s,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> bb;check(s->GetBuffer(s->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&bb)));barrier(list.Get(),from,state,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(list.Get(),bb.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(bb.Get(),from);barrier(list.Get(),bb.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);barrier(list.Get(),from,D3D12_RESOURCE_STATE_COPY_SOURCE,state);submit(false);}
// Do not infer a pacing interval from time that already includes the previous
// FG presents: feeding that delay back into the next frame halves base FPS.
template<class Call> HRESULT present(IDXGISwapChain3* s,UINT flags,Call original){
    if(recording)return original();
    std::unique_lock guard(mutex);pacedFrame=false;if(!requested||paused||s!=chain||(flags&(DXGI_PRESENT_TEST|DXGI_PRESENT_DO_NOT_WAIT))){guard.unlock();return original();}recording=true;struct Guard{~Guard(){recording=false;}} guardRecording;
    bool wrote=false;try{
        // No captured world frame, duplicate present, or unsubmitted recording: use vanilla presentation.
        if(!depth||!producerTick||frame==lastFrame)return original();
        auto now=std::chrono::steady_clock::now();auto elapsed=now-lastRealTime;lastRealTime=now;
        if(elapsed>std::chrono::milliseconds(250)){lastFrame=0;status="Waiting for continuous world rendering";return original();}
        if(!prepare(s))return original();check(queue->Wait(producerFence.Get(),producerTick));
        ComPtr<ID3D12Resource> bb;check(s->GetBuffer(s->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&bb)));
        barrier(list.Get(),bb.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(list.Get(),real.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(real.Get(),bb.Get());barrier(list.Get(),real.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(list.Get(),bb.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
        barrier(list.Get(),real.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(list.Get(),realOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(realOutput.Get(),real.Get());barrier(list.Get(),real.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(list.Get(),realOutput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(list.Get(),depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto o=options();o.multiFrameCount=std::min(requested,maximum);bool success=true;
        for(unsigned i=0;i<o.multiFrameCount;i++){o.multiFrameIndex=i+1;NVSDK_NGX_D3D12_DLSSG_Eval_Params e{};e.pBackbuffer=real.Get();e.pDepth=depth.Get();e.pMVecs=motion.Get();e.pOutputInterpFrame=interpolated[i].Get();e.pOutputRealFrame=realOutput.Get();e.pOutputDisableInterpolation=disable.Get();auto r=NGX_D3D12_EVALUATE_DLSSG(list.Get(),feature,params,&e,&o);if(evaluations<8||NVSDK_NGX_FAILED(r))report("NGX_D3D12_EVALUATE_DLSSG",unsigned(r),i+1);if(NVSDK_NGX_FAILED(r)){success=false;break;}evaluations++;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&b);}
        barrier(list.Get(),depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);barrier(list.Get(),disable.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(disableRead.Get(),0,disable.Get(),0,4);barrier(list.Get(),disable.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();lastFrame=frame;
        void* ptr=nullptr;D3D12_RANGE range{0,4};check(disableRead->Map(0,&range,&ptr));bool suppress=*static_cast<unsigned char*>(ptr)!=0;disableRead->Unmap(0,nullptr);
        if(!success||suppress){status=success?"Warming up frame history":"Frame Generation evaluation failed";return original();}
        const unsigned syncOverride=0;pacedFrame=false;
        for(unsigned i=0;i<o.multiFrameCount;i++){copyFrame(interpolated[i].Get(),s,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);wrote=true;auto hr=original(syncOverride,false);if(FAILED(hr)){report("IDXGISwapChain::Present(generated)",unsigned(hr),i+1);requested=0;break;}presented++;if(presented<=8)report("IDXGISwapChain::Present(generated)",unsigned(hr),i+1);}
        // Flip-model presentation advances the index each time. Restore the real image into the new current buffer.
        copyFrame(realOutput.Get(),s,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);wrote=false;status="Frame Generation active";return original(syncOverride);
    }catch(...){pacedFrame=false;requested=0;status="Frame Generation stopped after a graphics error";report("FG exception / disabled",unsigned(device?device->GetDeviceRemovedReason():E_FAIL));if(wrote&&real&&device&&SUCCEEDED(device->GetDeviceRemovedReason()))try{copyFrame(real.Get(),s,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}catch(...){}return original();}
}
template<class Call> HRESULT tracePresent(IDXGISwapChain* s,UINT sync,UINT flags,bool realFrame,Call original){
    LARGE_INTEGER begin{},end{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);
    auto result=original();QueryPerformanceCounter(&end);UINT presentId=0;auto countResult=s->GetLastPresentCount(&presentId);
    if(!(flags&DXGI_PRESENT_TEST)){std::lock_guard guard(traceMutex);deliveryTrace<<begin.QuadPart<<','<<end.QuadPart<<','<<frequency.QuadPart<<','<<GetCurrentThreadId()<<','<<reinterpret_cast<uintptr_t>(s)<<','<<presentId<<','<<(realFrame?"REAL":"GENERATED")<<','<<frame<<','<<sync<<','<<flags<<','<<unsigned(result)<<','<<unsigned(countResult)<<'\n';if(presentId%120==0)deliveryTrace.flush();}
    return result;
}
HRESULT STDMETHODCALLTYPE hookPresent(IDXGISwapChain* s,UINT sync,UINT flags){if(recording)return originalPresent(s,sync,flags);ComPtr<IDXGISwapChain3> c;s->QueryInterface(IID_PPV_ARGS(&c));auto before=presented;auto hr=present(c.Get(),flags,[&](UINT unused=0,bool realFrame=true){return tracePresent(s,sync,flags,realFrame,[&]{return originalPresent(s,sync,flags);});});if(!(flags&DXGI_PRESENT_TEST)){rates(before);sleep();}return hr;}
HRESULT STDMETHODCALLTYPE hookPresent1(IDXGISwapChain1* s,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p){if(recording)return originalPresent1(s,sync,flags,p);ComPtr<IDXGISwapChain3> c;s->QueryInterface(IID_PPV_ARGS(&c));auto before=presented;auto hr=present(c.Get(),flags,[&](UINT unused=0,bool realFrame=true){return tracePresent(s,sync,flags,realFrame,[&]{return originalPresent1(s,sync,flags,p);});});if(!(flags&DXGI_PRESENT_TEST)){rates(before);sleep();}return hr;}
void onPresent(reshade::api::command_queue* q,reshade::api::swapchain* s,const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){
    if(q->get_device()->get_api()!=reshade::api::device_api::d3d12)return;std::lock_guard guard(mutex);
    auto native=reinterpret_cast<IDXGISwapChain3*>(s->get_native());if(chain!=native){retire();chain=native;lastRealTime={};}
    queue=reinterpret_cast<ID3D12CommandQueue*>(q->get_native());
#ifdef FG_DIAGNOSTIC
    return; // Managed dispatcher owns the one presentation hook.
#endif
    if(!originalPresent){auto table=*reinterpret_cast<void***>(chain);MH_Initialize();auto a=MH_CreateHook(table[8],&hookPresent,reinterpret_cast<void**>(&originalPresent));auto b=MH_CreateHook(table[22],&hookPresent1,reinterpret_cast<void**>(&originalPresent1));report("MH_CreateHook(native Present)",unsigned(a));report("MH_CreateHook(native Present1)",unsigned(b));if(a==MH_OK)MH_EnableHook(table[8]);if(b==MH_OK)MH_EnableHook(table[22]);}
}
void destroySwapchain(reshade::api::swapchain* s,bool){std::lock_guard guard(mutex);if(chain==reinterpret_cast<IDXGISwapChain3*>(s->get_native())){retire();chain=nullptr;queue.Reset();refreshMonitor=nullptr;refreshHz=0;lastFrame=0;producer=nullptr;producerTick=0;}}
}
FgState fgState(){std::lock_guard guard(fg::mutex);return {fg::requested,fg::maximum,fg::evaluations,fg::presented,fg::lastResult,fg::status,fg::renderedFps,fg::presentedFps,fg::cadence,fg::refreshHz};}
void fgSelect(unsigned count){std::lock_guard guard(fg::mutex);fg::requested=std::min(count,fg::maximum);fg::lastFrame=0;fg::status=count?"Waiting for a world frame":"Off";fg::reflex(count!=0);fg::report("Menu Frame Generation selection",1,fg::requested);}
void fgCadence(unsigned divisor){std::lock_guard guard(fg::mutex);fg::cadence=std::min(divisor,4u);fg::report("Menu display refresh divisor (0=throughput)",1,fg::cadence);}
void fgPause(bool pause){std::lock_guard guard(fg::mutex);if(fg::paused&&!pause)fg::lastFrame=0;fg::paused=pause;}
bool fgRecording(){return fg::recording;}
void fgCapture(ID3D12GraphicsCommandList* l,ID3D12Resource* z,ID3D12Resource* mv,const unsigned char* cb,unsigned w,unsigned h,unsigned f){try{fg::capture(l,z,mv,cb,w,h,f);}catch(...){std::lock_guard guard(fg::mutex);fg::requested=0;fg::status="Frame Generation guide capture failed";fg::report("FG guide capture failed",unsigned(E_FAIL));}}
void fgSubmitted(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* lists){if(fg::recording)return;std::lock_guard guard(fg::mutex);if(!fg::producer||fg::producerTick)return;for(UINT i=0;i<n;i++)if(lists[i]==fg::producer){static UINT64 serial=0;fg::producerTick=++serial;auto hr=q->Signal(fg::producerFence.Get(),fg::producerTick);if(FAILED(hr)){fg::producerTick=0;fg::report("Guide queue Signal",unsigned(hr));}break;}}
void fgInitialize(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);auto dir=std::filesystem::path(path).parent_path()/L"fg";std::filesystem::create_directories(dir);
#ifdef FG_DIAGNOSTIC
fg::log.open(dir/L"diagnostic-events.jsonl");fg::deliveryTrace.open(dir/L"diagnostic-calls.csv");
#else
fg::log.open(dir/L"events.jsonl");fg::deliveryTrace.open(dir/L"delivery-calls.csv");
#endif
fg::deliveryTrace<<"begin_qpc,end_qpc,qpc_frequency,thread_id,swapchain,present_id,type,world_frame,sync_interval,flags,hresult,count_hresult\n";fg::deliveryTrace.flush();reshade::register_event<reshade::addon_event::present>(&fg::onPresent);reshade::register_event<reshade::addon_event::destroy_swapchain>(&fg::destroySwapchain);}
void fgShutdown(){/* The process owns the native presentation hooks until exit. */}
#ifdef FG_TEST
void fgTestBind(IDXGISwapChain3* s,ID3D12CommandQueue* q,const wchar_t* path){fg::chain=s;fg::queue=q;fg::maximum=5;fg::log.open(path);fg::deliveryTrace.open(std::filesystem::path(path).parent_path()/L"delivery-calls.csv");fg::deliveryTrace<<"begin_qpc,end_qpc,qpc_frequency,thread_id,swapchain,present_id,type,world_frame,sync_interval,flags,hresult,count_hresult\n";auto table=*reinterpret_cast<void***>(s);MH_Initialize();auto a=MH_CreateHook(table[8],&fg::hookPresent,reinterpret_cast<void**>(&fg::originalPresent));auto b=MH_CreateHook(table[22],&fg::hookPresent1,reinterpret_cast<void**>(&fg::originalPresent1));if(a!=MH_OK||b!=MH_OK)throw std::runtime_error("Presentation hook test failed");MH_EnableHook(table[8]);MH_EnableHook(table[22]);}
HRESULT fgTestPresent(IDXGISwapChain3* s){return s->Present(1,0);}
#endif
