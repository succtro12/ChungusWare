#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <chrono>
#include <bit>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include "nvsdk_ngx.h"
NVSDK_NGX_Result NVSDK_CONV bridgeNgxEvaluate(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
NVSDK_NGX_Result NVSDK_CONV bridgeNgxCreate(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
#define NVSDK_NGX_D3D12_EvaluateFeature bridgeNgxEvaluate
#define NVSDK_NGX_D3D12_CreateFeature bridgeNgxCreate
#define NVSDK_NGX_D3D12_EvaluateFeature_C bridgeNgxEvaluate
#define NVSDK_NGX_Parameter_SetF(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetI(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetUI(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetD3d12Resource(p,n,v) (p)->Set(n,v)
#define NVSDK_NGX_Parameter_SetVoidPointer(p,n,v) (p)->Set(n,v)
#include "nvsdk_ngx_helpers_dlssd_d3d.h"
#undef NVSDK_NGX_D3D12_EvaluateFeature
#undef NVSDK_NGX_D3D12_CreateFeature
#include "bridge_capture.h"
#include "raw_lighting.h"
#include "guide_conversion.h"
#include "bridge_unpack.h"
#include "rr_menu.h"
#include "frame_generation.h"
using Microsoft::WRL::ComPtr;
namespace bridge {
HMODULE module;std::mutex mutex;std::ofstream log;std::filesystem::path directory;
thread_local bool recording=false;
BridgeCloneTables cloneTables=nullptr;BridgeBindClone bindClone=nullptr;BridgeRestore preserve=nullptr;
decltype(&NVSDK_NGX_D3D12_CreateFeature) createFeature=nullptr;
decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluateFeature=nullptr;
decltype(&NVSDK_NGX_D3D12_GetCapabilityParameters) getCapabilities=nullptr;
decltype(&NVSDK_NGX_D3D12_DestroyParameters) destroyParameters=nullptr;
decltype(&NVSDK_NGX_D3D12_ReleaseFeature) releaseFeature=nullptr;
ComPtr<ID3D12Device> device;bedrock_rr::GuideConversion guides;bedrock_rr::BridgeUnpack unpack;
NVSDK_NGX_Handle* feature=nullptr;NVSDK_NGX_Parameter* parameters=nullptr;
unsigned width=0,height=0,allocationHeight=0,allocationWidth=0,targetWidth=0,targetHeight=0,mode=0,evaluations=0;
unsigned liveWidth=0,liveHeight=0,liveOutputWidth=0,liveOutputHeight=0;bool rrDisplayed=false;
uint64_t logSkips=0;unsigned lastEvaluatedFrame=0;bool outputRead=false;
unsigned sceneDomain=2,lastSceneDomain=2,historyResets=0;
std::chrono::steady_clock::time_point lastTime;
struct Slot {
    bool busy=false,raw=false,prepared=false,evaluated=false,readbackPending=false,independentGuides=false;unsigned frame=0,sceneDomain=0;
    ID3D12GraphicsCommandList* producer=nullptr;ID3D12GraphicsCommandList* consumer=nullptr;
    std::array<unsigned char,1792> camera{};
    bedrock_rr::RawLightingSnapshot snapshot;
    std::array<ComPtr<ID3D12Resource>,3> physicalGuides;
    std::array<ComPtr<ID3D12Resource>,7> inputs;
    std::array<ComPtr<ID3D12Resource>,6> combineOutputs;
    ComPtr<ID3D12Resource> combineAuxiliary;
    ComPtr<ID3D12Resource> output,domain,readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 readbackBytes=0,domainReadbackOffset=0;;
    ComPtr<ID3D12DescriptorHeap> guideHeap,unpackHeap,combineHeap,handoffHeap;
    ComPtr<ID3D12Fence> fence;ComPtr<ID3D12CommandQueue> queue;UINT64 fenceValue=0;
};
std::array<Slot*,3> slots{};
std::string hex(uint64_t value){std::ostringstream s;s<<"0x"<<std::hex<<value;return s.str();}
void emit(const std::string& event){log<<event<<'\n';log.flush();}
void skip(const char* reason,unsigned frame){static std::unordered_map<std::string,unsigned> counts;if(counts[reason]++<12)emit("{\"event\":\"bridge_skip\",\"frame\":"+std::to_string(frame)+",\"reason\":\""+reason+"\"}");}
float readFloat(const unsigned char* p,unsigned offset){float v;memcpy(&v,p+offset,4);return v;}
unsigned readUint(const unsigned char* p,unsigned offset){unsigned v;memcpy(&v,p+offset,4);return v;}
const BridgeBinding* find(const BridgePass& pass,char kind,unsigned reg,unsigned space=0){for(unsigned i=0;i<pass.bindingCount;i++){auto& b=pass.bindings[i];if(b.kind==kind&&b.reg==reg&&b.space==space)return &b;}return nullptr;}
ComPtr<ID3D12Resource> texture(unsigned w,unsigned h,DXGI_FORMAT format){
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.Format=format;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;if(FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&r))))throw std::runtime_error("Bridge texture allocation failed");return r;
}
ComPtr<ID3D12DescriptorHeap> heap(unsigned count){D3D12_DESCRIPTOR_HEAP_DESC d{};d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;d.NumDescriptors=count;d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> h;if(FAILED(device->CreateDescriptorHeap(&d,IID_PPV_ARGS(&h))))throw std::runtime_error("Bridge descriptor allocation failed");return h;}
D3D12_CPU_DESCRIPTOR_HANDLE cpu(ID3D12DescriptorHeap* heap,unsigned index){auto h=heap->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(index)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);return h;}
void srv(ID3D12DescriptorHeap* heap,unsigned index,ID3D12Resource* resource,DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format==DXGI_FORMAT_UNKNOWN?resource->GetDesc().Format:format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;device->CreateShaderResourceView(resource,&d,cpu(heap,index));}
void uav(ID3D12DescriptorHeap* heap,unsigned index,ID3D12Resource* resource){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=resource->GetDesc().Format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(resource,nullptr,&d,cpu(heap,index));}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
void ordering(ID3D12GraphicsCommandList* list){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&b);}
Slot* allocateSlot(){
    auto s=new Slot;
    if(!s->snapshot.initialize(device.Get(),allocationWidth,allocationHeight))throw std::runtime_error(s->snapshot.error());
    for(auto& r:s->physicalGuides)r=texture(allocationWidth,allocationHeight,DXGI_FORMAT_R16G16B16A16_FLOAT);
    for(unsigned i=0;i<7;i++)s->inputs[i]=texture(width,height,i==4||i==6?DXGI_FORMAT_R32_FLOAT:i==5?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT);
    s->output=texture(width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
    s->domain=texture(1,1,DXGI_FORMAT_R32_FLOAT);
    s->guideHeap=heap(7);s->unpackHeap=heap(17);s->combineHeap=heap(8192);s->handoffHeap=heap(3);
    return s;
}
float halfFloat(unsigned short value){unsigned sign=unsigned(value&0x8000)<<16,exp=(value>>10)&31,mantissa=value&1023,bits=0;
    if(exp==31)bits=sign|0x7f800000|(mantissa<<13);else if(exp)bits=sign|((exp+112)<<23)|(mantissa<<13);else if(mantissa){int e=-14;while(!(mantissa&1024)){mantissa<<=1;--e;}bits=sign|(unsigned(e+127)<<23)|((mantissa&1023)<<13);}else bits=sign;
    return std::bit_cast<float>(bits);
}
void pollReadback(){if(outputRead)return;for(auto s:slots)if(s&&s->readbackPending&&s->readback&&s->fence&&s->fenceValue&&s->fence->GetCompletedValue()>=s->fenceValue){
    void* data=nullptr;D3D12_RANGE range{0,SIZE_T(s->readbackBytes)};if(FAILED(s->readback->Map(0,&range,&data)))return;
    auto outputDesc=s->output->GetDesc();unsigned rw=unsigned(outputDesc.Width),rh=outputDesc.Height;
    unsigned invalid=0;double sum=0;for(unsigned y=0;y<rh;y++){auto row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(data)+s->footprint.Offset+size_t(y)*s->footprint.Footprint.RowPitch);for(unsigned x=0;x<rw;x++)for(unsigned c=0;c<3;c++){float v=halfFloat(row[x*4+c]);if(!std::isfinite(v))invalid++;else sum+=std::abs(v);}}
    float domainFactor=0;memcpy(&domainFactor,static_cast<const char*>(data)+s->domainReadbackOffset,4);emit("{\"event\":\"rr_domain_gpu_snapshot\",\"frame\":"+std::to_string(s->frame)+",\"scene_domain\":"+std::to_string(s->sceneDomain)+",\"native_handoff_multiplier\":"+std::to_string(domainFactor)+"}");
    std::ofstream dump(directory/L"first-rr-frame.rgba16f",std::ios::binary);dump.write(static_cast<const char*>(data),s->readbackBytes);s->readback->Unmap(0,nullptr);
    emit("{\"event\":\"rr_gpu_completed\",\"frame\":"+std::to_string(s->frame)+",\"fence_value\":"+std::to_string(s->fenceValue)+",\"width\":"+std::to_string(rw)+",\"height\":"+std::to_string(rh)+",\"row_pitch\":"+std::to_string(s->footprint.Footprint.RowPitch)+",\"nonfinite_rgb\":"+std::to_string(invalid)+",\"mean_abs_rgb\":"+std::to_string(sum/(rw*rh*3))+",\"device_removed_reason\":\""+hex(unsigned(device->GetDeviceRemovedReason()))+"\"}");outputRead=true;s->readbackPending=false;return;
}}
void queueSubmitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList*const* lists){if(recording||fgRecording())return;fgSubmitted(queue,count,lists);std::lock_guard guard(mutex);for(auto s:slots)if(s&&s->busy&&!s->fenceValue){auto submittedList=s->evaluated?s->consumer:s->producer;bool found=false;for(UINT i=0;i<count;i++)if(lists[i]==submittedList)found=true;if(!found)continue;
    if(!s->fence)device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s->fence));s->queue=queue;
    if(!s->fence){skip("Fence allocation failed",s->frame);continue;}
    auto hr=queue->Signal(s->fence.Get(),1);if(SUCCEEDED(hr))s->fenceValue=1;
    if(s->evaluated&&evaluations<=4)emit("{\"event\":\"rr_native_submission\",\"frame\":"+std::to_string(s->frame)+",\"list\":\""+hex(uintptr_t(submittedList))+"\",\"queue\":\""+hex(uintptr_t(queue))+"\",\"queue_type\":"+std::to_string(queue->GetDesc().Type)+",\"signal_result\":\""+hex(unsigned(hr))+"\",\"fence_value\":1}");}
}
void pass(const BridgePass* p){std::lock_guard lock(mutex);if(!p||p->cameraBytes<1600||recording)return;recording=true;struct Guard{~Guard(){recording=false;}} guard;
    auto frame=readUint(p->camera,1352);auto w=readFloat(p->camera,1248),h=readFloat(p->camera,1252);
    if(!std::isfinite(w)||!std::isfinite(h)||w<16||h<16||w>8192||h>8192)return;
    if(readUint(p->camera,1460)!=0||readUint(p->camera,1532)!=0){skip("Only validated checkerboard RTX with TAA disabled is enabled",frame);return;}
    pollReadback();
    // During an OS resize, ViewCB can expose the new dimensions before the
    // descriptor table switches to new native lighting allocations. Never
    // size a new RR generation from those transient old descriptors.
    if(p->shader==0x17e950ce2bdc6a38ull||p->shader==0x7905c7545a3c35b3ull){auto raw=find(*p,'U',5);if(!raw||!raw->resource)return;auto desc=raw->resource->GetDesc();if(desc.Width<unsigned(w)||desc.Width>unsigned(w)+1||desc.Height<unsigned(h)||desc.Height>unsigned(h)+1){skip("Resize waiting for matching native raw allocations",frame);return;}}
    if(!device){p->list->GetDevice(IID_PPV_ARGS(&device));if(!device||!guides.initialize(device.Get())||!unpack.initialize(device.Get())){skip("Guide or unpack pipeline creation failed",frame);return;}width=unsigned(w);height=unsigned(h);auto raw=find(*p,'U',5);if(!raw||!raw->resource)return;allocationHeight=raw->resource->GetDesc().Height;allocationWidth=unsigned(raw->resource->GetDesc().Width);}
    auto nativeRaw=find(*p,'U',5);
    bool rawAllocationChanged=(p->shader==0x17e950ce2bdc6a38ull||p->shader==0x7905c7545a3c35b3ull)&&nativeRaw&&nativeRaw->resource&&(nativeRaw->resource->GetDesc().Height!=allocationHeight||nativeRaw->resource->GetDesc().Width!=allocationWidth);
    if(width!=unsigned(w)||height!=unsigned(h)||rawAllocationChanged){
        if(p->shader!=0x17e950ce2bdc6a38ull&&p->shader!=0x7905c7545a3c35b3ull)return;
        for(auto slot:slots)if(slot&&slot->busy&&(!slot->fenceValue||!slot->fence||slot->fence->GetCompletedValue()<slot->fenceValue)){skip("Resize waiting for old native GPU submissions",frame);return;}
        pollReadback();
        if(feature){auto result=releaseFeature(feature);emit("{\"event\":\"rr_resize_release\",\"result\":\""+hex(unsigned(result))+"\"}");if(NVSDK_NGX_FAILED(result))return;feature=nullptr;}
        if(parameters){destroyParameters(parameters);parameters=nullptr;}
        auto raw=find(*p,'U',5);if(!raw||!raw->resource)return;
        for(auto& slot:slots){delete slot;slot=nullptr;}
        width=unsigned(w);height=unsigned(h);allocationHeight=raw->resource->GetDesc().Height;allocationWidth=unsigned(raw->resource->GetDesc().Width);evaluations=0;lastEvaluatedFrame=0;outputRead=false;
        emit("{\"event\":\"rr_resize_ready\",\"frame\":"+std::to_string(frame)+",\"render\":["+std::to_string(width)+","+std::to_string(height)+"],\"allocation_height\":"+std::to_string(allocationHeight)+",\"allocation_width\":"+std::to_string(allocationWidth)+",\"display_mode\":"+std::to_string(mode)+"}");
    }
    bool diffuse=p->shader==0x17e950ce2bdc6a38ull||p->shader==0x7905c7545a3c35b3ull;
    if(p->shader==0xa2c049a5a3f59ee4ull){static unsigned boundaryLogs=0;if(boundaryLogs++<24)emit("{\"event\":\"native_final_boundary\",\"frame\":"+std::to_string(frame)+",\"list\":\""+hex(uintptr_t(p->list))+"\",\"constants\":"+std::to_string(p->constants[0])+"}");}
    Slot* s=nullptr;for(auto slot:slots)if(slot&&slot->busy&&slot->frame==frame)s=slot;
    if(diffuse){if(s)return;for(auto& slot:slots){if(slot&&slot->busy&&slot->fenceValue&&slot->fence->GetCompletedValue()>=slot->fenceValue)slot->busy=false;
            if(!slot)slot=allocateSlot();if(!slot->busy){s=slot;break;}}
        if(!s){skip("All bridge buffers are in flight",frame);return;}s->busy=true;s->raw=false;s->prepared=false;s->evaluated=false;s->independentGuides=false;s->frame=frame;s->producer=p->list;s->consumer=nullptr;s->fenceValue=0;s->fence.Reset();s->queue.Reset();memcpy(s->camera.data(),p->camera,1792);return;}
    if(!s)return;
    if(p->shader==0xe91e519842717ad4ull){if(s->producer!=p->list){skip("Raw producers are on different lists",frame);return;}
        bool changed=p->actualShader&&p->actualShader!=p->shader;
        std::array<ID3D12Resource*,4> source{};std::array<D3D12_RESOURCE_STATES,4> sourceStates;sourceStates.fill(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);unsigned regs[]={5,6,28,15};for(unsigned i=0;i<4;i++){auto b=find(*p,'U',regs[i]);if(!b||!b->resource||(changed&&!b->stateKnown)||(b->stateKnown&&b->state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS&&!(changed&&i==3&&(b->state==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE||b->state==(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE))))){skip("Raw producer resource/state mismatch",frame);return;}source[i]=b->resource;if(changed)sourceStates[i]=b->state;}
        if(!s->snapshot.record(p->list,source,sourceStates)){skip("Native raw snapshot contract mismatch",frame);return;}s->raw=true;
        if(evaluations<2)emit("{\"event\":\"same_frame_raw_snapshot\",\"frame\":"+std::to_string(frame)+",\"shader\":\"0xe91e519842717ad4\",\"list\":\""+hex(uintptr_t(p->list))+"\",\"diffuse\":\""+hex(uintptr_t(source[0]))+"\",\"specular\":\""+hex(uintptr_t(source[1]))+"\",\"chroma\":\""+hex(uintptr_t(source[2]))+"\",\"hit_distance\":\""+hex(uintptr_t(source[3]))+"\"}");return;
    }
    if(p->shader!=0xa2c049a5a3f59ee4ull||!s->raw||s->prepared)return;
    if(s->producer!=p->list){skip("Raw and final combine are on different lists",frame);return;}
    unsigned offsets[64],counts[64];if(!cloneTables(p->list,s->combineHeap.Get(),offsets,counts)){skip("Native descriptor definitions could not be cloned",frame);return;}
    unsigned diffuseIndex=p->constants[0]&255,specularIndex=(p->constants[0]>>8)&255;if(diffuseIndex>=4||specularIndex>=8||diffuseIndex==specularIndex){skip("Unexpected dynamic denoiser indices",frame);return;}
    auto overrideUav=[&](unsigned reg,unsigned space,ID3D12Resource* resource){auto b=find(*p,'U',reg,space);if(!b||offsets[b->root]==UINT_MAX)return false;uav(s->combineHeap.Get(),offsets[b->root]+b->offset,resource);return true;};
    auto overrideSrv=[&](unsigned reg,unsigned space,ID3D12Resource* resource){auto b=find(*p,'S',reg,space);if(!b||offsets[b->root]==UINT_MAX)return false;srv(s->combineHeap.Get(),offsets[b->root]+b->offset,resource);return true;};
    bool srvLighting=p->lightingSrvSpace!=0;
    if(srvLighting){if(!overrideSrv(diffuseIndex,p->lightingSrvSpace,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::Diffuse))||!overrideSrv(specularIndex,p->lightingSrvSpace,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::Specular))||!overrideSrv(diffuseIndex+8,p->lightingSrvSpace,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::DiffuseChroma))){skip("SRV lighting input slots missing",frame);return;}}
    else if(!overrideUav(diffuseIndex,3,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::Diffuse))||!overrideUav(specularIndex,3,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::Specular))||!overrideUav(diffuseIndex+8,3,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::DiffuseChroma))){skip("Denoiser input slots missing",frame);return;}
    if(p->auxiliaryBuffer){
        auto b=find(*p,'U',0,14);auto& view=p->auxiliaryView;
        if(!b||!b->resource||!b->stateKnown||!p->auxiliaryViewKnown||p->auxiliaryCounter||offsets[b->root]==UINT_MAX||view.ViewDimension!=D3D12_UAV_DIMENSION_BUFFER||view.Format!=DXGI_FORMAT_UNKNOWN||view.Buffer.Flags!=D3D12_BUFFER_UAV_FLAG_NONE||!view.Buffer.StructureByteStride||!view.Buffer.NumElements){skip("Auxiliary combine buffer contract unavailable",frame);return;}
        auto desc=b->resource->GetDesc();auto elements=desc.Width/view.Buffer.StructureByteStride;
        if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER||desc.Width>1024*1024||view.Buffer.FirstElement>elements||view.Buffer.NumElements>elements-view.Buffer.FirstElement||!(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||(b->state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS&&(b->state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)==0)){skip("Auxiliary combine buffer contract unsupported",frame);return;}
        if(!s->combineAuxiliary||s->combineAuxiliary->GetDesc().Width!=desc.Width){D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> scratch;auto hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&scratch));if(FAILED(hr)){skip("Auxiliary combine buffer allocation failed",frame);return;}s->combineAuxiliary=scratch;}
        device->CreateUnorderedAccessView(s->combineAuxiliary.Get(),nullptr,&view,cpu(s->combineHeap.Get(),offsets[b->root]+b->offset));
        transition(p->list,b->resource,b->state,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(p->list,s->combineAuxiliary.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);p->list->CopyResource(s->combineAuxiliary.Get(),b->resource);transition(p->list,b->resource,D3D12_RESOURCE_STATE_COPY_SOURCE,b->state);transition(p->list,s->combineAuxiliary.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    unsigned outputRegs[]={12,16,17,18,23};for(unsigned i=0;i<5;i++){auto b=find(*p,'U',outputRegs[i]);if(!b||!b->resource){skip("Native combine output missing",frame);return;}if(!s->combineOutputs[i]){auto desc=b->resource->GetDesc();s->combineOutputs[i]=texture(unsigned(desc.Width),desc.Height,desc.Format);}if(!overrideUav(outputRegs[i],0,s->combineOutputs[i].Get()))return;}
    // A semantically classified replacement may use u14, which the exact
    // known-good shader declares but never writes. Redirect it as well; never
    // let an unknown replacement mutate an unowned native output on replay.
    if(p->actualShader&&p->actualShader!=p->shader&&p->extraOutput14){auto b=find(*p,'U',14);if(!b||!b->resource)return;auto desc=b->resource->GetDesc();if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)return;if(!s->combineOutputs[5])s->combineOutputs[5]=texture(unsigned(desc.Width),desc.Height,desc.Format);if(!overrideUav(14,0,s->combineOutputs[5].Get()))return;}
    // Execute the exact native material/SH/direct-light/volumetric composition,
    // substituting same-frame raw lobes and redirecting every written output.
    if(srvLighting)for(unsigned channel=0;channel<3;channel++)transition(p->list,s->snapshot.resource(static_cast<bedrock_rr::RawLightingSnapshot::Channel>(channel)),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bindClone(p->list,s->combineHeap.Get(),offsets);p->list->Dispatch(p->groups[0],p->groups[1],p->groups[2]);ordering(p->list);
    if(srvLighting)for(unsigned channel=0;channel<3;channel++)transition(p->list,s->snapshot.resource(static_cast<bedrock_rr::RawLightingSnapshot::Channel>(channel)),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    unsigned inputRegs[]={15,16,44,46};for(unsigned i=0;i<4;i++){auto b=find(*p,'S',inputRegs[i]);if(!b||!b->resource||!b->stateKnown||(b->state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)==0){skip("Guide read state was not established on this list",frame);return;}srv(s->guideHeap.Get(),i,b->resource,b->format);}
    for(unsigned i=0;i<3;i++)uav(s->guideHeap.Get(),4+i,s->physicalGuides[i].Get());
    if(!guides.record(p->list,s->guideHeap.Get(),s->guideHeap->GetGPUDescriptorHandleForHeapStart(),width,height)){skip(guides.error().c_str(),frame);return;}
    std::array<ID3D12Resource*,9> source={s->combineOutputs[3].Get(),s->physicalGuides[0].Get(),s->physicalGuides[1].Get(),s->physicalGuides[2].Get(),nullptr,nullptr,s->snapshot.resource(bedrock_rr::RawLightingSnapshot::SpecularDistance),nullptr,nullptr};
    unsigned boundRegs[]={14,21,19,46};unsigned positions[]={4,5,7,8};for(unsigned i=0;i<4;i++){auto b=find(*p,'S',boundRegs[i]);if(!b||!b->resource){skip("Unpack source missing",frame);return;}source[positions[i]]=b->resource;}
    for(unsigned i=0;i<9;i++)if(i==7){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Buffer.NumElements=UINT(source[i]->GetDesc().Width/16);view.Buffer.StructureByteStride=16;device->CreateShaderResourceView(source[i],&view,cpu(s->unpackHeap.Get(),i));}else srv(s->unpackHeap.Get(),i,source[i]);
    for(unsigned i=0;i<7;i++)uav(s->unpackHeap.Get(),9+i,s->inputs[i].Get());uav(s->unpackHeap.Get(),16,s->domain.Get());
    for(unsigned i:{0u,1u,2u,3u,6u})transition(p->list,source[i],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    float view[16];memcpy(view,p->camera,64);float scale=readFloat(p->camera,1248)<readFloat(p->camera,1264)?4.f:1.f;
    if(evaluations<2)emit("{\"event\":\"rr_exposure_domain\",\"frame\":"+std::to_string(frame)+",\"input\":\"linear_hdr\",\"scene_domain\":"+std::to_string(sceneDomain)+",\"duplicate_exposure_removed\":true,\"native_post_rr_exposure_adjustment\":"+std::to_string(scale==4.f)+"}");
    s->sceneDomain=sceneDomain;unpack.record(p->list,s->unpackHeap.Get(),s->unpackHeap->GetGPUDescriptorHandleForHeapStart(),width,height,unsigned(readFloat(p->camera,1280)),frame,readUint(p->camera,1460),scale,view,s->sceneDomain,p->shader==0x4453efec959d476aull);
    for(unsigned i:{0u,1u,2u,3u,6u})transition(p->list,source[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    s->prepared=true;
    // Changed renderer contracts publish independently validated guides at
    // their producer boundary. FG must not depend on RR feature evaluation.
    // Preserve the accepted known-hash vanilla capture boundary unchanged.
    if(p->actualShader&&p->actualShader!=p->shader){fgCapture(p->list,s->inputs[4].Get(),s->inputs[5].Get(),s->camera.data(),width,height,s->frame);s->independentGuides=true;}
    if(evaluations<2)emit("{\"event\":\"rr_inputs_prepared\",\"frame\":"+std::to_string(frame)+",\"combine_shader\":\"0xa2c049a5a3f59ee4\",\"actual_shader\":\""+hex(p->actualShader)+"\",\"lighting_srv_space\":"+std::to_string(p->lightingSrvSpace)+",\"auxiliary_isolated\":"+(p->auxiliaryBuffer?"true":"false")+",\"diffuse_index\":"+std::to_string(diffuseIndex)+",\"specular_index\":"+std::to_string(specularIndex)+",\"render\":["+std::to_string(width)+","+std::to_string(height)+"],\"allocation_height\":"+std::to_string(allocationHeight)+",\"sh_diffuse\":"+std::to_string(readUint(p->camera,1536))+"}");
}
bool runtime(){if(createFeature)return true;auto ngx=GetModuleHandleW(L"_nvngx.dll");if(!ngx)return false;
    createFeature=reinterpret_cast<decltype(createFeature)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_CreateFeature"));evaluateFeature=reinterpret_cast<decltype(evaluateFeature)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_EvaluateFeature"));getCapabilities=reinterpret_cast<decltype(getCapabilities)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_GetCapabilityParameters"));destroyParameters=reinterpret_cast<decltype(destroyParameters)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_DestroyParameters"));releaseFeature=reinterpret_cast<decltype(releaseFeature)>(GetProcAddress(ngx,"NVSDK_NGX_D3D12_ReleaseFeature"));return createFeature&&evaluateFeature&&getCapabilities&&destroyParameters&&releaseFeature;
}
void copyRect(ID3D12GraphicsCommandList* list,ID3D12Resource* to,ID3D12Resource* from){D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=to;src.pResource=from;dst.Type=src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_BOX rect{0,0,0,width,height,1};list->CopyTextureRegion(&dst,0,0,0,&src,&rect);}
struct SrCall {Slot* slot;ID3D12GraphicsCommandList* list;const NVSDK_NGX_Parameter* native;bool replacesSr=false;};
void recordEvaluation(void* data){auto& call=*static_cast<SrCall*>(data);auto s=call.slot;auto list=call.list;
    ID3D12Resource* nativeOutput=nullptr;
    call.native->Get(NVSDK_NGX_Parameter_Output,&nativeOutput);
    if(!nativeOutput){skip("Native SR output missing",s->frame);return;}
    auto destinationDesc=nativeOutput->GetDesc();
    unsigned ow=0,oh=0;
    call.native->Get(NVSDK_NGX_Parameter_OutWidth,&ow);call.native->Get(NVSDK_NGX_Parameter_OutHeight,&oh);
    if(!ow||!oh||ow>8192||oh>8192||destinationDesc.Width<ow||destinationDesc.Height<oh||destinationDesc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT){skip("Final RR output contract mismatch",s->frame);return;}
    extern bool currentState(ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES&);
    D3D12_RESOURCE_STATES outputState{};
    if(mode==2&&!currentState(list,nativeOutput,outputState)){
        // Bedrock 1.26.5203 prepares the double-buffered NGX output on a
        // different native command list: 192 -> 8 before evaluation, then
        // 8 -> 192 for composition. Verified in rr-output-state-diagnostic.
        // The per-list capture map therefore has no entry here. Keep this
        // fallback restricted to the verified one-subresource UAV layout.
        if(destinationDesc.MipLevels!=1||destinationDesc.DepthOrArraySize!=1||
           destinationDesc.SampleDesc.Count!=1||!(destinationDesc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)){
            skip("Native final output state/layout unavailable",s->frame);return;
        }
        outputState=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        static unsigned stateContracts=0;
        if(stateContracts++<4)emit("{\"event\":\"native_output_state_contract\",\"resource\":\""+hex(uintptr_t(nativeOutput))+"\",\"state\":8,\"source\":\"verified_bedrock_cross_list_ngx_output\"}");
    }
    const float renderRatio=float(width)/ow;
    if(width==ow&&height==oh){skip("Native-resolution RR removed; select Quality",s->frame);return;}
    const auto requestedQuality=renderRatio>=0.63f?NVSDK_NGX_PerfQuality_Value_MaxQuality:renderRatio>=0.54f?NVSDK_NGX_PerfQuality_Value_Balanced:renderRatio>=0.43f?NVSDK_NGX_PerfQuality_Value_MaxPerf:NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    if(feature&&(targetWidth!=ow||targetHeight!=oh)){
        for(auto old:slots)if(old&&old->evaluated&&(!old->fenceValue||!old->fence||old->fence->GetCompletedValue()<old->fenceValue)){skip("Output resize waiting for RR GPU completion",s->frame);return;}
        auto released=releaseFeature(feature);if(NVSDK_NGX_FAILED(released)){skip("RR output resize release failed",s->frame);return;}
        feature=nullptr;if(parameters){destroyParameters(parameters);parameters=nullptr;}evaluations=0;lastEvaluatedFrame=0;outputRead=false;
    }
    targetWidth=ow;targetHeight=oh;
    if(s->output->GetDesc().Width!=ow||s->output->GetDesc().Height!=oh){s->output=texture(ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT);s->readback.Reset();}
    if(!feature){auto result=getCapabilities(&parameters);if(NVSDK_NGX_FAILED(result)||!parameters){skip("Existing-session RR parameters unavailable",s->frame);return;}
        for(auto hint:{NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality})parameters->Set(hint,unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F));
        unsigned preset=0;auto presetResult=parameters->Get(NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,&preset);
        emit("{\"event\":\"rr_model_request\",\"preset\":\"F\",\"model\":\"RR2\",\"value\":"+std::to_string(preset)+",\"get_result\":\""+hex(unsigned(presetResult))+"\",\"all_quality_hints_set\":true}");
        NVSDK_NGX_DLSSD_Create_Params p{};p.InWidth=width;p.InHeight=height;p.InTargetWidth=targetWidth;p.InTargetHeight=targetHeight;
        p.InPerfQualityValue=requestedQuality;p.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;p.InDenoiseMode=NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;p.InRoughnessMode=NVSDK_NGX_DLSS_Roughness_Mode_Packed;p.InUseHWDepth=NVSDK_NGX_DLSS_Depth_Type_Linear;
        result=NGX_D3D12_CREATE_DLSSD_EXT(list,1,1,&feature,parameters,&p);
        emit("{\"event\":\"rr_quality_configuration\",\"api\":\"NGX_D3D12_CREATE_DLSSD_EXT\",\"frame\":"+std::to_string(s->frame)+",\"perf_quality\":"+std::to_string(int(requestedQuality))+",\"native_resolution\":"+std::to_string(width==targetWidth&&height==targetHeight)+",\"preset\":\"F\",\"result\":\""+hex(unsigned(result))+"\"}");
        emit("{\"event\":\"bridge_rr_create\",\"frame\":"+std::to_string(s->frame)+",\"result\":\""+hex(unsigned(result))+"\",\"handle\":\""+hex(uintptr_t(feature))+"\",\"render\":["+std::to_string(width)+","+std::to_string(height)+"],\"output\":["+std::to_string(targetWidth)+","+std::to_string(targetHeight)+"],\"dlaa\":"+std::to_string(width==targetWidth&&height==targetHeight)+",\"output_is_render_resolution\":false}");if(NVSDK_NGX_FAILED(result)||!feature)return;
    }
    ID3D12Resource* nativeMotion=nullptr,*nativeColour=nullptr;call.native->Get(NVSDK_NGX_Parameter_MotionVectors,&nativeMotion);call.native->Get(NVSDK_NGX_Parameter_Color,&nativeColour);
    if(!nativeMotion||!nativeColour){skip("Native SR motion/color missing",s->frame);return;}
    auto motionDesc=nativeMotion->GetDesc(),colourDesc=nativeColour->GetDesc();
    if(motionDesc.Format!=DXGI_FORMAT_R16G16_FLOAT||colourDesc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||motionDesc.Width<width||motionDesc.Height<height||colourDesc.Width<width||colourDesc.Height<height){skip("Native SR color/motion copy contract mismatch",s->frame);return;}
    // The existing native NGX call requires these resources to be shader-readable.
    // Resolve their exact recording state through capture before copying.
    extern bool currentState(ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES&);
    D3D12_RESOURCE_STATES motionState,colourState;if(!currentState(list,nativeMotion,motionState)||!currentState(list,nativeColour,colourState)){skip("Native SR resource states unavailable",s->frame);return;}
    transition(list,nativeMotion,motionState,D3D12_RESOURCE_STATE_COPY_SOURCE);transition(list,s->inputs[5].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);copyRect(list,s->inputs[5].Get(),nativeMotion);transition(list,s->inputs[5].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(list,nativeMotion,D3D12_RESOURCE_STATE_COPY_SOURCE,motionState);
    NVSDK_NGX_D3D12_DLSSD_Eval_Params p{};p.pInColor=s->inputs[0].Get();p.pInOutput=s->output.Get();p.pInNormals=s->inputs[1].Get();p.pInDiffuseAlbedo=s->inputs[2].Get();p.pInSpecularAlbedo=s->inputs[3].Get();p.pInDepth=s->inputs[4].Get();p.pInMotionVectors=s->inputs[5].Get();p.pInSpecularHitDistance=s->inputs[6].Get();
    float view[16],projection[16];memcpy(view,s->camera.data(),64);memcpy(projection,s->camera.data()+128,64);p.pInWorldToViewMatrix=view;p.pInViewToClipMatrix=projection;
    auto gx=call.native->Get(NVSDK_NGX_Parameter_MV_Scale_X,&p.InMVScaleX),gy=call.native->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&p.InMVScaleY);
    if(NVSDK_NGX_FAILED(gx)||NVSDK_NGX_FAILED(gy)||!p.InMVScaleX||!p.InMVScaleY){skip("Native motion scale parameters missing",s->frame);return;}
    call.native->Get(NVSDK_NGX_Parameter_Jitter_Offset_X,&p.InJitterOffsetX);call.native->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y,&p.InJitterOffsetY);
    if(evaluations<2){unsigned checkerHint=0;auto checkerResult=call.native->Get(NVSDK_NGX_Parameter_DLSS_Checkerboard_Jitter_Hack,&checkerHint);
        emit("{\"event\":\"native_checkerboard_contract\",\"frame\":"+std::to_string(s->frame)+",\"native_parameter\":\"DLSS.Checkerboard.Jitter.Hack\",\"get_result\":\""+hex(unsigned(checkerResult))+"\",\"value\":"+std::to_string(checkerHint)+",\"rr_parameter_forwarded\":false,\"render_method\":"+std::to_string(readUint(s->camera.data(),1460))+",\"rr_jitter\":["+std::to_string(p.InJitterOffsetX)+","+std::to_string(p.InJitterOffsetY)+"]}");
    }

    p.InPreExposure=1;p.InExposureScale=1;auto now=std::chrono::steady_clock::now();p.InFrameTimeDeltaInMsec=evaluations?float(std::chrono::duration<double,std::milli>(now-lastTime).count()):16.667f;lastTime=now;p.InFrameTimeDeltaInMsec=std::clamp(p.InFrameTimeDeltaInMsec,1.f,1000.f);p.InReset=!evaluations||s->frame!=lastEvaluatedFrame+1||s->sceneDomain!=lastSceneDomain;p.InRenderSubrectDimensions={width,height};
    for(auto& r:s->inputs)transition(list,r.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto result=NGX_D3D12_EVALUATE_DLSSD_EXT(list,feature,parameters,&p);ordering(list);
    for(auto& r:s->inputs)transition(list,r.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    unsigned frameGap=lastEvaluatedFrame?s->frame-lastEvaluatedFrame:0;bool succeeded=NVSDK_NGX_SUCCEED(result);if(succeeded){evaluations++;lastEvaluatedFrame=s->frame;lastSceneDomain=s->sceneDomain;if(p.InReset)historyResets++;}
    if(evaluations%120==0||p.InReset||!succeeded)emit("{\"event\":\"rr_temporal_health\",\"frame\":"+std::to_string(s->frame)+",\"evaluations\":"+std::to_string(evaluations)+",\"frame_gap\":"+std::to_string(frameGap)+",\"reset\":"+std::to_string(p.InReset)+",\"history_resets\":"+std::to_string(historyResets)+",\"scene_domain\":"+std::to_string(s->sceneDomain)+",\"result\":\""+hex(unsigned(result))+"\"}");
    if(evaluations<=8||!succeeded)emit("{\"event\":\"rr_evaluate_minecraft_frame\",\"call\":\"_nvngx.dll!NVSDK_NGX_D3D12_EvaluateFeature\",\"frame\":"+std::to_string(s->frame)+",\"list\":\""+hex(uintptr_t(list))+"\",\"raw_producer_list\":\""+hex(uintptr_t(s->producer))+"\",\"handle\":\""+hex(uintptr_t(feature))+"\",\"result\":\""+hex(unsigned(result))+"\",\"mv_scale\":["+std::to_string(p.InMVScaleX)+","+std::to_string(p.InMVScaleY)+"],\"jitter\":["+std::to_string(p.InJitterOffsetX)+","+std::to_string(p.InJitterOffsetY)+"],\"reset\":"+std::to_string(p.InReset)+",\"evaluations\":"+std::to_string(evaluations)+",\"display_mode\":"+std::to_string(mode)+"}");
    s->consumer=list;s->evaluated=true;
    if(succeeded&&!s->independentGuides)fgCapture(list,s->inputs[4].Get(),s->inputs[5].Get(),s->camera.data(),width,height,s->frame);
    if(succeeded&&!outputRead&&!s->readbackPending){auto desc=s->output->GetDesc();device->GetCopyableFootprints(&desc,0,1,0,&s->footprint,nullptr,nullptr,&s->readbackBytes);s->domainReadbackOffset=(s->readbackBytes+511)&~UINT64(511);s->readbackBytes=s->domainReadbackOffset+256;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=s->readbackBytes;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        s->readback.Reset();if(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s->readback)))){s->readbackPending=true;transition(list,s->output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=s->readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=s->footprint;src.pResource=s->output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(list,s->output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(list,s->domain.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);dst.PlacedFootprint={s->domainReadbackOffset,{DXGI_FORMAT_R32_FLOAT,1,1,1,256}};src.pResource=s->domain.Get();list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);transition(list,s->domain.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }}
    if((mode==1||mode==2)&&succeeded){
        auto source=mode==1?s->inputs[0].Get():s->output.Get();auto destination=mode==1?nativeColour:nativeOutput;auto nativeState=mode==1?colourState:outputState;
        unsigned outWidth=mode==1?width:targetWidth,outHeight=mode==1?height:targetHeight;
        srv(s->handoffHeap.Get(),0,source);srv(s->handoffHeap.Get(),1,s->domain.Get());uav(s->handoffHeap.Get(),2,destination);
        transition(list,source,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(list,s->domain.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(list,destination,nativeState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        unpack.handoff(list,s->handoffHeap.Get(),outWidth,outHeight);
        transition(list,destination,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nativeState);transition(list,s->domain.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(list,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(mode==2){call.replacesSr=true;static unsigned routed=0;
            if(routed++<16||p.InReset)emit("{\"event\":\"rr_solo_output_routed\",\"frame\":"+std::to_string(s->frame)+",\"destination\":\""+hex(uintptr_t(nativeOutput))+"\",\"input\":["+std::to_string(width)+","+std::to_string(height)+"],\"output\":["+std::to_string(targetWidth)+","+std::to_string(targetHeight)+"],\"native_sr_executed\":false,\"scene_domain\":"+std::to_string(s->sceneDomain)+",\"dlaa\":"+std::to_string(width==targetWidth&&height==targetHeight)+",\"result\":\""+hex(unsigned(result))+"\"}");
        }
    }

}
using StateQuery=bool(*)(ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES*);StateQuery stateQuery=nullptr;
bool currentState(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES& state){return stateQuery&&stateQuery(list,resource,&state);}
bool beforeSr(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Parameter* native){if(recording||!list||!native)return false;std::lock_guard guard(mutex);if(!runtime())return false;
    unsigned iw=0,ih=0;native->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,&iw);native->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,&ih);liveWidth=iw;liveHeight=ih;native->Get(NVSDK_NGX_Parameter_OutWidth,&liveOutputWidth);native->Get(NVSDK_NGX_Parameter_OutHeight,&liveOutputHeight);rrDisplayed=false;if(iw!=width||ih!=height){static unsigned logged=0;if(logged++<16)emit("{\"event\":\"rr_before_sr_rejected\",\"reason\":\"render dimensions do not match discovered RR producers\",\"requested_mode\":"+std::to_string(mode)+",\"native_render\":["+std::to_string(iw)+","+std::to_string(ih)+"],\"bridge_render\":["+std::to_string(width)+","+std::to_string(height)+"],\"list\":\""+hex(uintptr_t(list))+"\",\"native_sr_allowed\":true}");return false;}
    Slot* selected=nullptr;for(auto s:slots)if(s&&s->busy&&s->prepared&&!s->evaluated&&s->producer==list&&(!selected||s->frame>selected->frame))selected=s;
    if(!selected){skip("No same-list, same-frame RR inputs for native SR",0);return false;}
    if(mode==0&&selected->independentGuides){static unsigned logs=0;if(logs++<8)emit("{\"event\":\"native_sr_with_independent_fg_guides\",\"frame\":"+std::to_string(selected->frame)+",\"rr_evaluation_skipped\":true,\"list\":\""+hex(uintptr_t(list))+"\"}");return false;}
    recording=true;SrCall call{selected,list,native};try{preserve(list,&recordEvaluation,&call);}catch(...){skip("RR recording exception",selected->frame);}recording=false;rrDisplayed=call.replacesSr;return call.replacesSr;
}
DWORD WINAPI start(void*){wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);directory=std::filesystem::path(path).parent_path()/L"bridge";std::filesystem::create_directories(directory);log.open(directory/L"events.jsonl",std::ios::trunc);
    auto capture=GetModuleHandleW(L"bedrock_rr_capture.dll");if(!capture)return 1;auto registerCallback=reinterpret_cast<BridgeRegister>(GetProcAddress(capture,"BedrockRrRegisterBridge"));cloneTables=reinterpret_cast<BridgeCloneTables>(GetProcAddress(capture,"BedrockRrCloneTables"));bindClone=reinterpret_cast<BridgeBindClone>(GetProcAddress(capture,"BedrockRrBindClone"));preserve=reinterpret_cast<BridgeRestore>(GetProcAddress(capture,"BedrockRrPreserveCompute"));stateQuery=reinterpret_cast<StateQuery>(GetProcAddress(capture,"BedrockRrResourceState"));if(!registerCallback||!cloneTables||!bindClone||!preserve||!stateQuery)return 1;
    bool menuReady=rrMenuInitialize(module);emit(std::string("{\"event\":\"rr_menu_registration\",\"ready\":")+(menuReady?"true}":"false}"));
    registerCallback(&pass,&queueSubmitted);emit("{\"event\":\"renderer_bridge_ready\",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"initial_mode\":\"vanilla_with_rr_evaluation\",\"keys\":\"F8 menu\",\"rr_mode_replaces_sr\":true}");return 0;
}
}
NVSDK_NGX_Result NVSDK_CONV bridgeNgxCreate(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature feature,NVSDK_NGX_Parameter* params,NVSDK_NGX_Handle** handle){return bridge::createFeature(list,feature,params,handle);}
NVSDK_NGX_Result NVSDK_CONV bridgeNgxEvaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* params,PFN_NVSDK_NGX_ProgressCallback callback){return bridge::evaluateFeature(list,handle,params,callback);}
extern "C" __declspec(dllexport) bool BedrockRrBeforeSr(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Parameter* params){return bridge::beforeSr(list,params);}
extern "C" RrMenuState BedrockRrMenuState(){std::lock_guard guard(bridge::mutex);return {bridge::mode,bridge::sceneDomain,bridge::liveWidth,bridge::liveHeight,bridge::liveOutputWidth,bridge::liveOutputHeight,bridge::evaluations,bridge::rrDisplayed};}
extern "C" void BedrockRrMenuSet(unsigned mode,unsigned domain){if(mode>2||domain>2)return;std::lock_guard guard(bridge::mutex);bridge::mode=mode;if(domain!=bridge::sceneDomain){bridge::sceneDomain=domain;bridge::outputRead=false;}bridge::emit("{\"event\":\"rr_menu_changed\",\"mode\":"+std::to_string(mode)+",\"domain\":"+std::to_string(domain)+"}");}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){bridge::module=h;DisableThreadLibraryCalls(h);auto thread=CreateThread(nullptr,0,bridge::start,nullptr,0,nullptr);if(thread)CloseHandle(thread);}else if(reason==DLL_PROCESS_DETACH){rrMenuShutdown(h);}return TRUE;}
