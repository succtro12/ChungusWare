#include "frame_generation.h"
#include "device_identity.h"
#include <windows.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <MinHook.h>
#include <reshade.hpp>
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <sl_security.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <stdexcept>
#include <intrin.h>
#include <chrono>
#include <vector>
#ifdef CHUNGUSWARE_TEST_CONTROL
void rrTestControlPoll();
#endif
#ifdef RR_MANAGED_FG
FgState fgDiagnosticState();void fgDiagnosticSelect(unsigned);void fgDiagnosticPause(bool);void fgDiagnosticInitialize();bool fgDiagnosticRecording();void fgDiagnosticCapture(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*,const unsigned char*,unsigned,unsigned,unsigned);void fgDiagnosticSubmitted(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);HRESULT fgDiagnosticPresent(IDXGISwapChain3*,UINT,UINT,HRESULT(*)(void*),void*);
#endif
using Microsoft::WRL::ComPtr;
#include "fg_delivery_tracker.h"
#ifdef FG_MANAGED_TEST
using ObserveEvaluate=NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
ObserveEvaluate testOriginalEvaluate=nullptr;
NVSDK_NGX_Result NVSDK_CONV testEvaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback callback){fg_delivery::evaluate(p);return testOriginalEvaluate(list,handle,p,callback);}
void testObserveNgx(){if(testOriginalEvaluate)return;auto dll=GetModuleHandleW(L"_nvngx.dll");if(!dll)return;auto entry=GetProcAddress(dll,"NVSDK_NGX_D3D12_EvaluateFeature");if(entry&&MH_CreateHook(entry,&testEvaluate,reinterpret_cast<void**>(&testOriginalEvaluate))==MH_OK)MH_EnableHook(entry);}
#endif
namespace managed_fg {
std::recursive_mutex mutex;
thread_local bool internal=false;
bool ready=false,paused=false,configured=false;unsigned pacingMode=0;float targetFps=0,refreshHz=0;unsigned lastWorldFrame=0;
unsigned requested=0,maximum=1,evaluations=0,presented=0,lastResult=0,frame=0,sequence=0;
unsigned skipped=0;
const char* status="Off";
float renderedFps=0,presentedFps=0;unsigned sampleRendered=0,samplePresented=0;auto sampleBegin=std::chrono::steady_clock::now();
auto lastRefreshQuery=std::chrono::steady_clock::time_point{};
std::ofstream log,runtimeLog;
HMODULE interposer=nullptr;
PFun_slInit* init=nullptr;
PFun_slUpgradeInterface* upgrade=nullptr;
PFun_slSetD3DDevice* setDevice=nullptr;
PFun_slGetNativeInterface* getNative=nullptr;
PFun_slGetFeatureFunction* getFunction=nullptr;
PFun_slGetNewFrameToken* getToken=nullptr;
PFun_slSetConstants* setConstants=nullptr;
PFun_slSetTagForFrame* setTags=nullptr;
PFun_slIsFeatureSupported* supported=nullptr;
PFun_slDLSSGSetOptions* setOptions=nullptr;
PFun_slDLSSGGetState* getState=nullptr;
PFun_slReflexSetOptions* reflexOptions=nullptr;
PFun_slReflexSleep* reflexSleep=nullptr;
PFun_slPCLSetMarker* marker=nullptr;
sl::ViewportHandle viewport(0);
sl::FrameToken* token=nullptr;
ComPtr<ID3D12Device> device;
ComPtr<ID3D12Device> queueDeviceProxy;
ComPtr<ID3D12RootSignature> root;
ComPtr<ID3D12PipelineState> pipeline;
IDXGISwapChain3* chain=nullptr;
std::mutex clientQueuesMutex;std::vector<ID3D12CommandQueue*> clientQueues;
using CreateDevice=HRESULT(WINAPI*)(IUnknown*,D3D_FEATURE_LEVEL,REFIID,void**);
using CreateQueue=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_COMMAND_QUEUE_DESC*,REFIID,void**);
using Present=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using Resize1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
CreateDevice originalDevice=nullptr;
CreateQueue originalQueue=nullptr;
Present originalPresent=nullptr;
Present1 originalPresent1=nullptr;
Resize1 originalResize1=nullptr;
void report(const char*,unsigned,unsigned);
HRESULT STDMETHODCALLTYPE resize1(IDXGISwapChain3* swap,UINT count,UINT w,UINT h,DXGI_FORMAT format,UINT flags,const UINT* nodes,IUnknown*const* queues){
 // Bedrock supplies one presenting queue for every buffer. The managed FG
 // chain may expand its physical buffer count: ResizeBuffers avoids passing
 // client-sized node/queue arrays into that larger physical allocation.
 bool singleQueue=count&&queues;IUnknown* identity=nullptr;
 for(UINT i=0;singleQueue&&i<count;i++){
    if(nodes&&nodes[i]>1){singleQueue=false;break;}
    ComPtr<IUnknown> native;void* value=nullptr;
    if(getNative&&getNative(queues[i],&value)==sl::Result::eOk&&value)native.Attach(static_cast<IUnknown*>(value));else native=queues[i];
    ComPtr<IUnknown> canonical;native.As(&canonical);if(!canonical){singleQueue=false;break;}
    if(!i)identity=canonical.Get();else if(identity!=canonical.Get())singleQueue=false;
 }
 auto result=singleQueue?swap->ResizeBuffers(count,w,h,format,flags):originalResize1(swap,count,w,h,format,flags,nodes,queues);
 report(singleQueue?"Managed single-queue ResizeBuffers1 via ResizeBuffers":"Managed ResizeBuffers1",unsigned(result),count);return result;
}
struct Snapshot {
 ComPtr<ID3D12Resource> depth,motion;
 ComPtr<ID3D12DescriptorHeap> heap;
 ComPtr<ID3D12Fence> producerFence,inputsFence;
 UINT64 producerValue=0,inputsValue=0;
 ID3D12GraphicsCommandList* producer=nullptr;
 unsigned width=0,height=0,worldFrame=0;
 bool pending=false,tagged=false;
 std::array<unsigned char,1792> camera{};
};
std::array<Snapshot,8> snapshots;
Snapshot* newest=nullptr;
Snapshot* lastTagged=nullptr;
void check(HRESULT result){if(FAILED(result))throw std::runtime_error("Managed FG D3D12 operation failed");}
void report(const char* name,unsigned result,unsigned count=0){lastResult=result;log<<"{\"call\":\""<<name<<"\",\"result\":"<<result<<",\"world_frame\":"<<frame<<",\"render_sequence\":"<<sequence<<",\"count\":"<<count<<",\"actual_presentations\":"<<presented<<"}\n";log.flush();}
bool ok(const char* name,sl::Result result){if(sequence<8||result!=sl::Result::eOk)report(name,unsigned(result));return result==sl::Result::eOk;}
void runtimeMessage(sl::LogType,const char* message){if(runtimeLog)runtimeLog<<message<<std::endl;}
template<class T> bool import(T*& target,const char* name){target=reinterpret_cast<T*>(GetProcAddress(interposer,name));return target!=nullptr;}
template<class T> bool feature(T*& target,sl::Feature id,const char* name){void* address=nullptr;auto result=getFunction(id,name,address);report(name,unsigned(result));target=reinterpret_cast<T*>(address);return result==sl::Result::eOk&&target;}
struct Internal {bool before=internal;Internal(){internal=true;}~Internal(){internal=before;}};
ComPtr<IUnknown> nativeIdentity(IUnknown* value){
 constexpr GUID unwrap={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
 ComPtr<IUnknown> native,identity;if(!value)return identity;
 if(FAILED(value->QueryInterface(unwrap,reinterpret_cast<void**>(native.GetAddressOf()))))native=value;
 native.As(&identity);return identity;
}
HRESULT clientQueue(ID3D12Device* proxy,const D3D12_COMMAND_QUEUE_DESC* desc,REFIID iid,void** out){
 auto hr=proxy->CreateCommandQueue(desc,iid,out);if(SUCCEEDED(hr)&&out&&*out){void* native=nullptr;auto r=getNative(*out,&native);if(r==sl::Result::eOk&&native){static_cast<IUnknown*>(*out)->Release();*out=native;}ComPtr<ID3D12CommandQueue> queue;static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&queue));if(queue){ComPtr<ID3D12Device> d;queue->GetDevice(IID_PPV_ARGS(&d));fg_delivery::device(d.Get(),queue.Get());std::lock_guard guard(clientQueuesMutex);clientQueues.push_back(queue.Get());}}return hr;
}
void barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
ComPtr<ID3D12Resource> texture(unsigned w,unsigned h,DXGI_FORMAT format,D3D12_RESOURCE_STATES state){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;}
void setupDepth(){
 const char* shader=R"(Texture2D<float> source:register(t0);RWTexture2D<float> dest:register(u0);cbuffer C:register(b0){float A,B,C,D;uint W,H;}[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){if(p.x>=W||p.y>=H)return;float z=sign(C)*source[p.xy];dest[p.xy]=saturate((A*z+B)/(C*z+D));})";
 ComPtr<ID3DBlob> code,errors,blob;check(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"main","cs_5_1",0,0,&code,&errors));D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,6};D3D12_ROOT_SIGNATURE_DESC desc{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root.Get();ps.CS={code->GetBufferPointer(),code->GetBufferSize()};check(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline)));
}
HRESULT STDMETHODCALLTYPE hookQueue(ID3D12Device* native,const D3D12_COMMAND_QUEUE_DESC* desc,REFIID iid,void** out){
 if(internal||!ready||!desc||desc->Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return originalQueue(native,desc,iid,out);
 HMODULE caller=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(_ReturnAddress()),&caller);
 // NVIDIA's own worker queues are not client queues. Forward them directly.
 if(caller!=GetModuleHandleW(nullptr)&&caller!=GetModuleHandleW(L"dxgi.dll"))return originalQueue(native,desc,iid,out);
 std::lock_guard guard(mutex);Internal scope;
 if(queueDeviceProxy)return clientQueue(queueDeviceProxy.Get(),desc,iid,out);
 if(!ok("slSetD3DDevice(queue creation)",setDevice(native)))return originalQueue(native,desc,iid,out);
 ID3D12Device* proxy=native;proxy->AddRef();auto result=upgrade(reinterpret_cast<void**>(&proxy));report("slUpgradeInterface(device for CreateCommandQueue)",unsigned(result));
 if(result!=sl::Result::eOk){proxy->Release();return originalQueue(native,desc,iid,out);}
 queueDeviceProxy.Attach(proxy);auto hr=clientQueue(proxy,desc,iid,out);report("Streamline CreateCommandQueue(native client queue)",unsigned(hr));return hr;
}
HRESULT WINAPI hookDevice(IUnknown* adapter,D3D_FEATURE_LEVEL level,REFIID iid,void** out){
 auto hr=originalDevice(adapter,level,iid,out);if(internal||FAILED(hr)||!out||!*out||!ready)return hr;
 std::lock_guard guard(mutex);Internal scope;ComPtr<ID3D12Device> wrapped;static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&wrapped));if(!wrapped)return hr;auto identity=bedrock_rr::deviceIdentity(wrapped.Get());ComPtr<ID3D12Device> native;identity.As(&native);if(!native)return hr;
 if(!originalQueue){auto entry=(*reinterpret_cast<void***>(native.Get()))[8];auto result=MH_CreateHook(entry,&hookQueue,reinterpret_cast<void**>(&originalQueue));report("MH_CreateHook(CreateCommandQueue)",unsigned(result));if(result==MH_OK)MH_EnableHook(entry);}
 if(adapter){ComPtr<IDXGIAdapter1> a;if(SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&a)))){DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);sl::AdapterInfo info{};info.deviceLUID=reinterpret_cast<uint8_t*>(&d.AdapterLuid);info.deviceLUIDSizeInBytes=sizeof(LUID);report("slIsFeatureSupported(DLSS_G)",unsigned(supported(sl::kFeatureDLSS_G,info)));log<<"{\"adapter_luid_high\":"<<d.AdapterLuid.HighPart<<",\"adapter_luid_low\":"<<d.AdapterLuid.LowPart<<"}\n";}}
 return hr;
}
bool initialize(){
 std::lock_guard guard(mutex);if(ready)return true;Internal scope;
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto rootDir=std::filesystem::path(exe).parent_path();auto logDir=rootDir/L"fg";std::filesystem::create_directories(logDir);if(!log.is_open())log.open(logDir/L"events.jsonl");if(!runtimeLog.is_open())runtimeLog.open(logDir/L"streamline.log");auto pluginDir=rootDir/L"bedrock-rr-streamline";auto dll=pluginDir/L"sl.interposer.dll";
 if(!fg_delivery::trace.is_open()){fg_delivery::trace.open(logDir/L"physical-calls.csv");fg_delivery::trace<<"begin_qpc,end_qpc,qpc_frequency,thread_id,swapchain,present_id,type,world_frame,sync_interval,flags,hresult,count_hresult,stats_hresult,displayed_present_count,present_refresh_count,sync_refresh_count,sync_qpc\n";fg_delivery::trace.flush();}
 delivery_telemetry::start(rootDir);
 if(!std::filesystem::exists(dll)||!sl::security::verifyEmbeddedSignature(dll.c_str())){status="Managed FG runtime missing or signature invalid";report("Verify Streamline NVIDIA signature",0);return false;}
 interposer=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!interposer){report("LoadLibraryEx(sl.interposer)",GetLastError());return false;}
 if(!import(init,"slInit")||!import(upgrade,"slUpgradeInterface")||!import(setDevice,"slSetD3DDevice")||!import(getNative,"slGetNativeInterface")||!import(getFunction,"slGetFeatureFunction")||!import(getToken,"slGetNewFrameToken")||!import(setConstants,"slSetConstants")||!import(setTags,"slSetTagForFrame")||!import(supported,"slIsFeatureSupported"))return false;
 auto plugins=pluginDir.wstring(),logs=logDir.wstring();const wchar_t* paths[]={plugins.c_str()};sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};sl::Preferences p{};p.flags=sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;p.pathsToPlugins=paths;p.numPathsToPlugins=1;p.pathToLogsAndData=logs.c_str();p.featuresToLoad=features;p.numFeaturesToLoad=3;
#ifdef FG_MANAGED_TEST
 p.applicationId=0;p.projectId="f68a0a36-9374-4e5b-b62f-d55f5d6c7810";
#else
 // This is Bedrock's verified own NGX identity, preserved for this integration.
 p.applicationId=100007911;
#endif
 p.engineVersion="1.26.5203.0";p.logMessageCallback=&runtimeMessage;p.logLevel=sl::LogLevel::eDefault;auto result=init(p,sl::kSDKVersion);report("slInit(manual, DLSS_G+Reflex+PCL)",unsigned(result));if(result!=sl::Result::eOk)return false;
 ready=true;HMODULE d3d=LoadLibraryExW(L"d3d12.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);auto entry=GetProcAddress(d3d,"D3D12CreateDevice");MH_Initialize();
 auto hook=MH_CreateHook(entry,&hookDevice,reinterpret_cast<void**>(&originalDevice));report("MH_CreateHook(D3D12CreateDevice)",unsigned(hook));if(hook!=MH_OK){ready=false;return false;}MH_EnableHook(entry);return true;
}
bool getFeatures(){if(setOptions&&getState&&reflexOptions&&reflexSleep&&marker)return true;bool valid=feature(setOptions,sl::kFeatureDLSS_G,"slDLSSGSetOptions")&&feature(getState,sl::kFeatureDLSS_G,"slDLSSGGetState")&&feature(reflexOptions,sl::kFeatureReflex,"slReflexSetOptions")&&feature(reflexSleep,sl::kFeatureReflex,"slReflexSleep")&&feature(marker,sl::kFeaturePCL,"slPCLSetMarker");if(valid){sl::DLSSGState state{};auto r=getState(viewport,state,nullptr);report("slDLSSGGetState(capabilities)",unsigned(r),state.numFramesToGenerateMax);if(r==sl::Result::eOk)maximum=std::clamp(state.numFramesToGenerateMax,1u,5u);}return valid;}
void configure(bool enabled){
 if(!getFeatures())return;sl::ReflexOptions r{};r.mode=enabled?sl::ReflexMode::eLowLatency:sl::ReflexMode::eOff;float target=targetFps>0?targetFps:refreshHz;r.frameLimitUs=enabled&&pacingMode==1&&target>0?unsigned(1000000.0/target):0;ok("slReflexSetOptions",reflexOptions(r));sl::DLSSGOptions o{};o.mode=enabled?sl::DLSSGMode::eOn:sl::DLSSGMode::eOff;o.numFramesToGenerate=std::max(1u,requested);o.flags=sl::DLSSGFlags::eDynamicResolutionEnabled;if(ok("slDLSSGSetOptions",setOptions(viewport,o)))configured=enabled;fg_delivery::featureEnabled=configured;report("Reflex limit microseconds",0,r.frameLimitUs);
}
sl::Constants constants(const Snapshot& s,unsigned ow,unsigned oh){
 using namespace DirectX;sl::Constants c{};auto mat=[&](unsigned offset){XMFLOAT4X4 m;memcpy(&m,s.camera.data()+offset,64);return XMLoadFloat4x4(&m);};auto save=[](sl::float4x4& out,FXMMATRIX m){XMFLOAT4X4 f;XMStoreFloat4x4(&f,m);memcpy(&out,&f,64);};save(c.cameraViewToClip,mat(128));save(c.clipToCameraView,mat(192));save(c.clipToLensClip,XMMatrixIdentity());auto cp=XMMatrixMultiply(mat(320),mat(384));save(c.clipToPrevClip,cp);save(c.prevClipToClip,XMMatrixInverse(nullptr,cp));float p[16],v[16],j[2];memcpy(p,s.camera.data()+128,64);memcpy(v,s.camera.data()+256,64);memcpy(j,s.camera.data()+1304,8);c.jitterOffset={-j[0],-j[1]};c.cameraPinholeOffset={0,0};c.mvecScale={1,1};c.cameraPos={v[12],v[13],v[14]};c.cameraRight={v[0],v[1],v[2]};c.cameraUp={v[4],v[5],v[6]};auto sign=p[11]<0?-1.f:1.f;c.cameraFwd={v[8]*sign,v[9]*sign,v[10]*sign};float a=std::abs(p[14]/p[10]),b=std::abs(p[14]/(p[10]-p[11]));c.cameraNear=std::min(a,b);c.cameraFar=std::max(a,b);c.cameraFOV=2*atanf(1/std::abs(p[5]));c.cameraAspectRatio=float(ow)/oh;c.depthInverted=b<a?sl::eTrue:sl::eFalse;c.cameraMotionIncluded=sl::eTrue;c.motionVectors3D=sl::eFalse;c.reset=!lastWorldFrame||s.worldFrame!=lastWorldFrame+1?sl::eTrue:sl::eFalse;return c;
}
void capture(ID3D12GraphicsCommandList* list,ID3D12Resource* z,ID3D12Resource* mv,const unsigned char* camera,unsigned w,unsigned h,unsigned worldFrame){
 std::lock_guard guard(mutex);if(!requested||paused||!ready)return;
 if(!device){check(list->GetDevice(IID_PPV_ARGS(&device)));setupDepth();}
 Snapshot* slot=nullptr;for(auto& s:snapshots)if(!s.pending&&!s.tagged&&(!s.producerValue||s.producerFence->GetCompletedValue()>=s.producerValue)&&(!s.inputsValue||s.inputsFence->GetCompletedValue()>=s.inputsValue)){slot=&s;break;}if(!slot)return;auto& s=*slot;
 if(s.width!=w||s.height!=h){s.depth=texture(w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.motion=texture(w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);s.width=w;s.height=h;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&s.heap)));if(!s.producerFence)check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.producerFence)));}
 memcpy(s.camera.data(),camera,1792);s.worldFrame=worldFrame;s.producer=list;s.pending=true;s.producerValue=0;auto handle=s.heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC sr{};sr.Format=DXGI_FORMAT_R32_FLOAT;sr.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sr.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sr.Texture2D.MipLevels=1;device->CreateShaderResourceView(z,&sr,handle);handle.ptr+=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);D3D12_UNORDERED_ACCESS_VIEW_DESC ua{};ua.Format=DXGI_FORMAT_R32_FLOAT;ua.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(s.depth.Get(),nullptr,&ua,handle);
 barrier(list,z,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(list,s.depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);float projection[16];memcpy(projection,camera+128,64);struct C{float a,b,c,d;unsigned w,h;} c{projection[10],projection[14],projection[11],projection[15],w,h};ID3D12DescriptorHeap* heaps[]={s.heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipeline.Get());list->SetComputeRootDescriptorTable(0,s.heap->GetGPUDescriptorHandleForHeapStart());list->SetComputeRoot32BitConstants(1,6,&c,0);list->Dispatch((w+7)/8,(h+7)/8,1);barrier(list,s.depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(list,z,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);barrier(list,mv,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);barrier(list,s.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(s.motion.Get(),mv);barrier(list,s.motion.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);barrier(list,mv,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 if(newest&&newest->pending)newest->pending=false;newest=&s;frame=worldFrame;
}
void afterPresent(){
 if(!setOptions)return;sl::DLSSGState s{};auto result=getState(viewport,s,nullptr);ok("slDLSSGGetState(after Present)",result);if(result==sl::Result::eOk){maximum=std::clamp(s.numFramesToGenerateMax,1u,5u);presented+=s.numFramesActuallyPresented;if(lastTagged){lastTagged->tagged=false;lastTagged->pending=false;lastTagged->inputsFence=reinterpret_cast<ID3D12Fence*>(s.inputsProcessingCompletionFence);lastTagged->inputsValue=s.lastPresentInputsProcessingCompletionFenceValue;lastTagged=nullptr;}if(sequence<8||sequence%120==0)report("Streamline actual presentations / status",unsigned(s.status),s.numFramesActuallyPresented);status=configured?(s.numFramesActuallyPresented>1?"Frame Generation active":"Frame Generation waiting / see live counters"):(paused?"Paused while menu is open":"Off");}
 if(configured&&result==sl::Result::eOk&&s.numFramesActuallyPresented<requested+1)skipped+=requested+1-s.numFramesActuallyPresented;
 ++sampleRendered;samplePresented+=s.numFramesActuallyPresented;auto now=std::chrono::steady_clock::now();auto seconds=std::chrono::duration<float>(now-sampleBegin).count();if(seconds>=1){renderedFps=sampleRendered/seconds;presentedFps=samplePresented/seconds;log<<"{\"event\":\"presentation_rates\",\"fg_enabled\":"<<(configured?"true":"false")<<",\"rendered_fps\":"<<renderedFps<<",\"presented_fps\":"<<presentedFps<<"}\n";log.flush();sampleBegin=now;sampleRendered=samplePresented=0;}
 ++sequence;ok("slGetNewFrameToken(next frame)",getToken(token,&sequence));if(configured&&token){auto before=std::chrono::steady_clock::now();ok("slReflexSleep(next real-frame boundary)",reflexSleep(*token));if(sequence%120==0)log<<"{\"event\":\"reflex_sleep_ms\",\"ms\":"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count()<<"}\n";}
}
template<class Call> HRESULT present(UINT flags,Call original){
 if(internal||(flags&DXGI_PRESENT_TEST))return original();Internal scope;sl::FrameToken* currentToken=nullptr;
 {std::lock_guard guard(mutex);currentToken=token;}
 if(currentToken&&marker)marker(sl::PCLMarker::ePresentStart,*currentToken);
 // Never hold the bridge mutex across NVIDIA's managed Present. Its worker
 // can submit queues concurrently and must not wait for this game's thread.
 auto before=std::chrono::steady_clock::now();auto result=original();if(currentToken&&marker)marker(sl::PCLMarker::ePresentEnd,*currentToken);
 if(sequence%120==0)log<<"{\"event\":\"managed_present_ms\",\"ms\":"<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count()<<"}\n";
 if(SUCCEEDED(result)){std::lock_guard guard(mutex);afterPresent();}return result;
}
HRESULT STDMETHODCALLTYPE hookPresent(IDXGISwapChain* s,UINT sync,UINT flags){
#ifdef RR_MANAGED_FG
 if(pacingMode==2&&!internal){struct Context{IDXGISwapChain* chain;UINT sync,flags;} c{s,sync,flags};ComPtr<IDXGISwapChain3> native;s->QueryInterface(IID_PPV_ARGS(&native));return fgDiagnosticPresent(native.Get(),sync,flags,[](void* ptr){auto& c=*static_cast<Context*>(ptr);return originalPresent(c.chain,c.sync,c.flags);},&c);}
#endif
 return present(flags,[&]{return originalPresent(s,sync,flags);});}
HRESULT STDMETHODCALLTYPE hookPresent1(IDXGISwapChain1* s,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p){
#ifdef RR_MANAGED_FG
 if(pacingMode==2&&!internal){struct Context{IDXGISwapChain1* chain;UINT sync,flags;const DXGI_PRESENT_PARAMETERS* params;} c{s,sync,flags,p};ComPtr<IDXGISwapChain3> native;s->QueryInterface(IID_PPV_ARGS(&native));return fgDiagnosticPresent(native.Get(),sync,flags,[](void* ptr){auto& c=*static_cast<Context*>(ptr);return originalPresent1(c.chain,c.sync,c.flags,c.params);},&c);}
#endif
 return present(flags,[&]{return originalPresent1(s,sync,flags,p);});}
void prepareFrame(ID3D12CommandQueue* queue,IDXGISwapChain3* current){
#ifdef CHUNGUSWARE_TEST_CONTROL
 rrTestControlPoll();
#endif
 if(!ready)return;std::lock_guard guard(mutex);Internal scope;
 if(chain!=current){chain=current;configured=false;token=nullptr;sequence=0;}
 if(!getFeatures()){status="Managed FG feature initialization failed";return;}
#ifdef FG_MANAGED_TEST
 testObserveNgx();
#endif
 if(!originalPresent){auto table=*reinterpret_cast<void***>(chain);auto a=MH_CreateHook(table[8],&hookPresent,reinterpret_cast<void**>(&originalPresent));auto b=MH_CreateHook(table[22],&hookPresent1,reinterpret_cast<void**>(&originalPresent1));auto c=MH_CreateHook(table[39],&resize1,reinterpret_cast<void**>(&originalResize1));report("MH_CreateHook(Streamline proxy Present)",unsigned(a));report("MH_CreateHook(Streamline proxy Present1)",unsigned(b));report("MH_CreateHook(Streamline proxy ResizeBuffers1)",unsigned(c));if(a==MH_OK)MH_EnableHook(table[8]);if(b==MH_OK)MH_EnableHook(table[22]);if(c==MH_OK)MH_EnableHook(table[39]);}
 auto now=std::chrono::steady_clock::now();if(now-lastRefreshQuery>=std::chrono::seconds(1)){lastRefreshQuery=now;ComPtr<IDXGIOutput> output;DXGI_OUTPUT_DESC outputDesc{};DEVMODEW display{};display.dmSize=sizeof(display);if(SUCCEEDED(chain->GetContainingOutput(&output))&&SUCCEEDED(output->GetDesc(&outputDesc))&&EnumDisplaySettingsW(outputDesc.DeviceName,ENUM_CURRENT_SETTINGS,&display)&&display.dmDisplayFrequency>1)refreshHz=float(display.dmDisplayFrequency);}
 bool enabled=pacingMode!=2&&requested&&!paused&&newest&&newest->producerValue;
 if(requested&&!enabled){const char* reason=pacingMode==2?"diagnostic pacing mode":paused?"explicit pause":!newest?"no captured depth/motion/camera producer":"producer has no submitted queue fence";static std::string previous;static unsigned logged=0;if(previous!=reason&&logged++<32){previous=reason;log<<"{\"event\":\"fg_prepare_rejected\",\"reason\":\""<<reason<<"\",\"requested\":"<<requested<<",\"world_frame\":"<<frame<<",\"swapchain\":\""<<chain<<"\",\"queue\":\""<<queue<<"\",\"configured\":false}\n";log.flush();}}
 if(enabled!=configured)configure(enabled);if(!enabled)return;
 if(!token&&!ok("slGetNewFrameToken(first frame)",getToken(token,&sequence)))return;auto& s=*newest;frame=s.worldFrame;auto nativeQueue=queue;check(nativeQueue->Wait(s.producerFence.Get(),s.producerValue));DXGI_SWAP_CHAIN_DESC1 d{};chain->GetDesc1(&d);auto c=constants(s,d.Width,d.Height);if(!ok("slSetConstants",setConstants(c,*token,viewport)))return;
 sl::Resource z{sl::ResourceType::eTex2d,s.depth.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},mv{sl::ResourceType::eTex2d,s.motion.Get(),nullptr,nullptr,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};sl::Extent e{0,0,s.width,s.height};sl::ResourceTag tags[]={{&z,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&e},{&mv,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&e}};if(ok("slSetTagForFrame(depth+motion)",setTags(*token,viewport,tags,2,nullptr))){s.tagged=true;s.pending=false;lastTagged=&s;newest=nullptr;lastWorldFrame=s.worldFrame;evaluations++;}
}
void onPresent(reshade::api::command_queue* q,reshade::api::swapchain* s,const reshade::api::rect*,const reshade::api::rect*,uint32_t,const reshade::api::rect*){if(q->get_device()->get_api()!=reshade::api::device_api::d3d12)return;try{prepareFrame(reinterpret_cast<ID3D12CommandQueue*>(q->get_native()),reinterpret_cast<IDXGISwapChain3*>(s->get_native()));}catch(...){std::lock_guard guard(mutex);requested=0;status="Managed FG presentation preparation failed";report("prepareFrame exception",unsigned(E_FAIL));}}
void destroySwapchain(reshade::api::swapchain* s,bool){std::lock_guard guard(mutex);if(chain==reinterpret_cast<IDXGISwapChain3*>(s->get_native())){if(configured)configure(false);chain=nullptr;token=nullptr;newest=nullptr;lastTagged=nullptr;for(auto& slot:snapshots){slot.pending=false;slot.tagged=false;}}}
}
#include "fg_factory_bridge.h"
extern "C" __declspec(dllexport) void BedrockFgObserveEvaluate(const NVSDK_NGX_Parameter* p){fg_delivery::evaluate(p);}
extern "C" __declspec(dllexport) BOOL WINAPI BedrockFgNativeNgxLists(){return managed_fg::ready?TRUE:FALSE;}
FgDeliveryState fgDeliveryState(){return delivery_telemetry::state();}
#ifdef CHUNGUSWARE_TEST_CONTROL
extern "C" __declspec(dllexport) void ChungusWareTestDiagnostics(){
 std::lock_guard guard(managed_fg::mutex);managed_fg::log<<"{\"event\":\"test_diagnostics\",\"physical_calls\":"<<fg_delivery::physicalCalls.load()<<",\"trace_open\":"<<fg_delivery::trace.is_open()<<",\"trace_good\":"<<fg_delivery::trace.good()<<",\"native_evaluations\":"<<fg_delivery::observedEvaluations<<",\"client_queues\":"<<managed_fg::clientQueues.size()<<"}\n";managed_fg::log.flush();
}
#endif
extern "C" __declspec(dllexport) BOOL WINAPI FgUpgradeFactory(void** factory){try{if(managed_fg::internal||!factory||!*factory||!managed_fg::initialize())return FALSE;std::lock_guard guard(managed_fg::mutex);managed_fg::Internal scope;return managed_fg::hookNativeFactory(static_cast<IUnknown*>(*factory));}catch(...){return FALSE;}}
FgState fgState(){std::lock_guard guard(managed_fg::mutex);
#ifdef RR_MANAGED_FG
 if(managed_fg::pacingMode==2){auto state=fgDiagnosticState();state.pacingMode=2;state.refreshHz=managed_fg::refreshHz;state.targetFps=managed_fg::targetFps;return state;}
#endif
 return {managed_fg::requested,managed_fg::maximum,managed_fg::evaluations,managed_fg::presented,managed_fg::lastResult,managed_fg::status,managed_fg::renderedFps,managed_fg::presentedFps,0,managed_fg::refreshHz,managed_fg::pacingMode,managed_fg::targetFps,managed_fg::configured,managed_fg::skipped};}
void fgSelect(unsigned count){std::lock_guard guard(managed_fg::mutex);if(managed_fg::configured)managed_fg::configure(false);managed_fg::requested=std::min(count,managed_fg::maximum);
#ifdef RR_MANAGED_FG
 fgDiagnosticSelect(managed_fg::pacingMode==2?count:0);
#endif
 managed_fg::lastWorldFrame=0;managed_fg::status=count?"Waiting for a world frame":"Off";if(!count&&managed_fg::ready&&managed_fg::setOptions)managed_fg::configure(false);managed_fg::report("Menu Frame Generation selection",0,count);}
void fgPacing(unsigned mode,float target){std::lock_guard guard(managed_fg::mutex);mode=std::min(mode,2u);if(managed_fg::configured)managed_fg::configure(false);managed_fg::pacingMode=mode;managed_fg::targetFps=std::clamp(target,0.f,1000.f);managed_fg::lastWorldFrame=0;
#ifdef RR_MANAGED_FG
 fgDiagnosticSelect(mode==2?managed_fg::requested:0);
#endif
 managed_fg::report("F8 pacing mode (0=managed,1=steady,2=diagnostic)",0,mode);}
void fgPause(bool pause){std::lock_guard guard(managed_fg::mutex);if(managed_fg::paused&&!pause)managed_fg::lastWorldFrame=0;managed_fg::paused=pause;
#ifdef RR_MANAGED_FG
 fgDiagnosticPause(pause||managed_fg::pacingMode!=2);
#endif
}
bool fgRecording(){return managed_fg::internal
#ifdef RR_MANAGED_FG
 ||fgDiagnosticRecording()
#endif
 ;}
void fgCapture(ID3D12GraphicsCommandList* l,ID3D12Resource* z,ID3D12Resource* mv,const unsigned char* cb,unsigned w,unsigned h,unsigned f){
#ifdef RR_MANAGED_FG
 if(managed_fg::pacingMode==2){fgDiagnosticCapture(l,z,mv,cb,w,h,f);return;}
#endif
 try{managed_fg::capture(l,z,mv,cb,w,h,f);}catch(...){std::lock_guard guard(managed_fg::mutex);managed_fg::requested=0;managed_fg::status="Frame Generation guide error";managed_fg::report("Guide capture exception",unsigned(E_FAIL));}}
void fgSubmitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){
 if(managed_fg::internal)return;auto queueIdentity=managed_fg::nativeIdentity(queue);bool client=false;
 {std::lock_guard guard(managed_fg::clientQueuesMutex);for(auto q:managed_fg::clientQueues)if(managed_fg::nativeIdentity(q).Get()==queueIdentity.Get()){client=true;break;}}
 if(!client)return;
#ifdef RR_MANAGED_FG
 if(managed_fg::pacingMode==2){fgDiagnosticSubmitted(queue,count,lists);return;}
#endif
 std::lock_guard guard(managed_fg::mutex);static UINT64 serial=0;for(auto& s:managed_fg::snapshots)if(s.producer&&!s.producerValue)for(UINT i=0;i<count;i++)if(managed_fg::nativeIdentity(lists[i]).Get()==managed_fg::nativeIdentity(s.producer).Get()){s.producerValue=++serial;auto hr=queue->Signal(s.producerFence.Get(),s.producerValue);if(serial<=4||FAILED(hr))managed_fg::report("Guide producer Signal",unsigned(hr));if(FAILED(hr))s.producerValue=0;s.producer=nullptr;break;}}
void fgInitialize(){
#ifdef RR_MANAGED_FG
 fgDiagnosticInitialize();fgDiagnosticPause(true);
#endif
 reshade::register_event<reshade::addon_event::present>(&managed_fg::onPresent);reshade::register_event<reshade::addon_event::destroy_swapchain>(&managed_fg::destroySwapchain);}
void fgShutdown(){delivery_telemetry::stop();std::lock_guard guard(managed_fg::mutex);if(!managed_fg::ready)return;managed_fg::Internal scope;if(managed_fg::configured)managed_fg::configure(false);auto shutdown=reinterpret_cast<PFun_slShutdown*>(GetProcAddress(managed_fg::interposer,"slShutdown"));if(shutdown)managed_fg::report("slShutdown",unsigned(shutdown()));managed_fg::ready=false;managed_fg::managedFactories.clear();managed_fg::queueDeviceProxy.Reset();}

#ifdef FG_MANAGED_TEST
void fgManagedTestPrepare(IDXGISwapChain3* s,ID3D12CommandQueue* q){managed_fg::prepareFrame(q,s);}
#endif
