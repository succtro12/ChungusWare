#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <vector>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include "hook_slots.h"
#include "bridge_capture.h"
#include "shader_contract.h"
using Microsoft::WRL::ComPtr;
namespace {
HMODULE module;
HANDLE captureReadyEvent=nullptr;
std::atomic_bool captureReady{false};
std::filesystem::path folder;
std::mutex mutex;
std::ofstream logFile;
std::atomic_uint64_t dispatchCount{0};
constexpr uint64_t maxDispatches=12000;
std::unordered_map<std::string,unsigned> occurrences;
struct View { uintptr_t resource=0; D3D12_RESOURCE_DESC desc{}; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; char kind='?'; UINT64 address=0; UINT size=0;
    bool explicitDesc=false; D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; D3D12_UNORDERED_ACCESS_VIEW_DESC uav{}; uintptr_t counter=0; };
struct Range { UINT root=0,offset=0,count=0,reg=0,space=0; D3D12_DESCRIPTOR_RANGE_TYPE type{}; UINT actualCount=0; };
struct Root { uint64_t hash=0; std::vector<Range> ranges; };
struct Heap { UINT64 gpu=0; SIZE_T cpu=0; UINT count=0,stride=0; };
struct State {
    uint64_t shader=0;
    uintptr_t signature=0;
    std::array<UINT64,64> tables{},cbvs{};
    std::array<std::vector<UINT>,64> constants;
    std::vector<Heap> heaps;
    std::vector<ID3D12DescriptorHeap*> nativeHeaps;
    ID3D12PipelineState* pipeline=nullptr;
    // Observed recording order on this list, not inferred queue execution state.
    // Unknown resources and partial/split transitions remain unknown.
    std::unordered_map<uintptr_t,D3D12_RESOURCE_STATES> transitions;
    std::unordered_set<uintptr_t> invalidStates;
    bool invalidatesInherited=false;
};
struct Mapping { UINT64 gpu=0,size=0; void* cpu=nullptr; };
std::unordered_map<SIZE_T,View> views;
std::unordered_map<void*,Root> roots;
std::unordered_map<void*,uint64_t> pipelines;
std::unordered_map<void*,State> states;
std::unordered_map<void*,Mapping> mappings;
struct PassEvidence {
    shader_contract::Profile profile;
    shader_contract::Candidate candidate;
    unsigned frames=0,lastFrame=UINT_MAX;
    unsigned invalidLogs=0;
    bool accepted=false,rejectedLogged=false,cached=false;
};
std::unordered_map<uint64_t,PassEvidence> passEvidence;
std::unordered_map<uint64_t,uint64_t> advisoryCache;
std::array<UINT,4> validatedRoleFrame{UINT_MAX,UINT_MAX,UINT_MAX,UINT_MAX};
std::array<UINT,4> candidateRoleFrame{UINT_MAX,UINT_MAX,UINT_MAX,UINT_MAX};
std::array<uint64_t,4> candidateRoleHash{};
std::atomic_bool changedRendererContract{false};
// Only used for unknown renderer contracts. An initial state remains evidence
// only until any transition/aliasing is recorded. Never infer cross-list order.
struct ObservedState {D3D12_RESOURCE_STATES state;uint64_t epoch;ID3D12CommandQueue* queue;};
std::unordered_map<uintptr_t,ObservedState> untouchedInitialStates,submittedStates;
std::unordered_map<void*,ID3D12CommandQueue*> listQueues;
std::unordered_map<ID3D12CommandQueue*,unsigned> queueActiveCalls;
std::unordered_set<ID3D12CommandQueue*> ambiguousQueues;
// Private graphics-object lifetime token; contains no user or system identity.
// It prevents a freed resource address from inheriting an earlier object's state.
constexpr GUID stateEpochGuid={0x64b1b16a,0xe968,0x4136,{0xa7,0x21,0x63,0x74,0x82,0x9f,0x2e,0x81}};
uint64_t epochSerial=0;
uint64_t resourceEpoch(ID3D12Resource* resource,bool create){uint64_t epoch=0;UINT size=sizeof(epoch);if(SUCCEEDED(resource->GetPrivateData(stateEpochGuid,&size,&epoch))&&size==sizeof(epoch)&&epoch)return epoch;if(!create)return 0;epoch=++epochSerial;return SUCCEEDED(resource->SetPrivateData(stateEpochGuid,sizeof(epoch),&epoch))?epoch:0;}
bool inheritedState(void* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES& state){
    if(!resource)return false;
    auto& s=states[list];auto key=uintptr_t(resource);
    if(s.invalidatesInherited||s.invalidStates.contains(key))return false;
    auto epoch=resourceEpoch(resource,false);if(!epoch)return false;
    auto initial=untouchedInitialStates.find(key);
    if(initial!=untouchedInitialStates.end()&&initial->second.epoch==epoch){state=initial->second.state;return true;}
    auto previous=submittedStates.find(key);auto q=listQueues.find(list);
    if(previous==submittedStates.end()||q==listQueues.end()||previous->second.epoch!=epoch||
        previous->second.queue!=q->second||ambiguousQueues.contains(q->second))return false;
    state=previous->second.state;return true;
}
std::atomic<BridgePassCallback> bridgeCallback{nullptr};
std::atomic<BridgeQueueCallback> bridgeQueue{nullptr};
thread_local bool bridgeExecuting=false;
thread_local std::array<unsigned char,1792> bridgeCamera{};
thread_local unsigned bridgeLightingSrvSpace=0;
thread_local bool bridgeAuxiliaryBuffer=false;
thread_local bool bridgeExtraOutput14=false;
std::string hex(uint64_t v){std::ostringstream s;s<<std::hex<<v;return s.str();}
uint64_t hash(const void* p,size_t n){uint64_t h=14695981039346656037ull;auto b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
void line(const std::string& s){if(logFile){logFile<<s<<'\n';logFile.flush();}}
uint64_t dump(const void* p,size_t n,const char* suffix){
    if(!p||!n||n>64*1024*1024)return 0;
    auto h=hash(p,n);auto path=folder/L"shaders"/(hex(h)+suffix);
    if(!std::filesystem::exists(path)){std::ofstream f(path,std::ios::binary);f.write(static_cast<const char*>(p),n);}
    return h;
}
bool knownPass(uint64_t h){return h==0x17e950ce2bdc6a38ull||h==0x7905c7545a3c35b3ull||h==0xe91e519842717ad4ull||h==0xa2c049a5a3f59ee4ull;}
void inspectPass(uint64_t h,const void* bytes,size_t size){
    if(knownPass(h)||passEvidence.contains(h))return;
    auto& e=passEvidence[h];e.profile=shader_contract::inspect(bytes,size);
    e.candidate=shader_contract::classify(e.profile);
    if(e.candidate.role!=shader_contract::Role::none)changedRendererContract=true;
    static bool cacheRead=false;
    if(!cacheRead){cacheRead=true;std::ifstream f(folder/L"pass-fingerprints-v1.txt");uint64_t hash=0,signature=0;unsigned entries=0;
        while(entries++<256&&(f>>std::hex>>hash>>signature))advisoryCache[hash]=signature;}
    auto fp=shader_contract::fingerprint(e.profile);
    e.cached=advisoryCache.contains(h)&&advisoryCache[h]==fp;
    if(e.candidate.role!=shader_contract::Role::none)
        line("{\"event\":\"pass_contract_candidate\",\"shader\":\""+hex(h)+"\",\"fingerprint\":\""+hex(fp)+"\",\"entry\":\""+e.profile.entry+"\",\"role\":"+std::to_string(unsigned(e.candidate.role))+",\"compatible\":"+(e.candidate.compatible?"true":"false")+",\"cached_advisory\":"+(e.cached?"true":"false")+",\"reason\":\""+e.candidate.reason+"\"}");
}
void observeInitialState(void* object,D3D12_RESOURCE_STATES state,const char* api="legacy resource creation"){
    if(!object||!changedRendererContract.load())return;
    ComPtr<ID3D12Resource> resource;
    if(FAILED(static_cast<IUnknown*>(object)->QueryInterface(IID_PPV_ARGS(&resource))))return;
    std::lock_guard guard(mutex);auto key=uintptr_t(resource.Get());submittedStates.erase(key);auto epoch=resourceEpoch(resource.Get(),true);if(epoch)untouchedInitialStates[key]={state,epoch,nullptr};
    auto desc=resource->GetDesc();static unsigned creationLogs=0;
    if(creationLogs<256&&desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&(desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||desc.Format==DXGI_FORMAT_R16_FLOAT)){++creationLogs;line("{\"event\":\"resource_initial_state\",\"api\":\""+std::string(api)+"\",\"resource\":\""+hex(key)+"\",\"state\":"+std::to_string(unsigned(state))+",\"format\":"+std::to_string(unsigned(desc.Format))+",\"width\":"+std::to_string(desc.Width)+",\"height\":"+std::to_string(desc.Height)+"}");}
}
using CommittedFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
using PlacedFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Heap*,UINT64,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
CommittedFn realCommitted;PlacedFn realPlaced;
HRESULT STDMETHODCALLTYPE committed(ID3D12Device* d,const D3D12_HEAP_PROPERTIES* h,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out){auto hr=realCommitted(d,h,flags,desc,state,clear,iid,out);if(SUCCEEDED(hr)&&out&&*out)try{observeInitialState(*out,state);}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE placed(ID3D12Device* d,ID3D12Heap* heap,UINT64 offset,const D3D12_RESOURCE_DESC* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out){auto hr=realPlaced(d,heap,offset,desc,state,clear,iid,out);if(SUCCEEDED(hr)&&out&&*out)try{observeInitialState(*out,state);}catch(...){}return hr;}
using Committed1Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device4*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,ID3D12ProtectedResourceSession*,REFIID,void**);
using Committed2Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device8*,const D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS,const D3D12_RESOURCE_DESC1*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,ID3D12ProtectedResourceSession*,REFIID,void**);
using Placed1Fn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device8*,ID3D12Heap*,UINT64,const D3D12_RESOURCE_DESC1*,D3D12_RESOURCE_STATES,const D3D12_CLEAR_VALUE*,REFIID,void**);
Committed1Fn realCommitted1;Committed2Fn realCommitted2;Placed1Fn realPlaced1;
HRESULT STDMETHODCALLTYPE committed1(ID3D12Device4* d,const D3D12_HEAP_PROPERTIES* h,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,ID3D12ProtectedResourceSession* session,REFIID iid,void** out){auto hr=realCommitted1(d,h,flags,desc,state,clear,session,iid,out);if(SUCCEEDED(hr)&&out&&*out)try{observeInitialState(*out,state,"CreateCommittedResource1");}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE committed2(ID3D12Device8* d,const D3D12_HEAP_PROPERTIES* h,D3D12_HEAP_FLAGS flags,const D3D12_RESOURCE_DESC1* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,ID3D12ProtectedResourceSession* session,REFIID iid,void** out){auto hr=realCommitted2(d,h,flags,desc,state,clear,session,iid,out);if(SUCCEEDED(hr)&&out&&*out)try{observeInitialState(*out,state,"CreateCommittedResource2");}catch(...){}return hr;}
HRESULT STDMETHODCALLTYPE placed1(ID3D12Device8* d,ID3D12Heap* heap,UINT64 offset,const D3D12_RESOURCE_DESC1* desc,D3D12_RESOURCE_STATES state,const D3D12_CLEAR_VALUE* clear,REFIID iid,void** out){auto hr=realPlaced1(d,heap,offset,desc,state,clear,iid,out);if(SUCCEEDED(hr)&&out&&*out)try{observeInitialState(*out,state,"CreatePlacedResource1");}catch(...){}return hr;}
void status(const char* value,const char* detail){
    std::ofstream f(folder/L"status.json");f<<"{\"status\":\""<<value<<"\",\"detail\":\""<<detail<<"\",\"mode\":\"capture-only\",\"dispatch_limit\":"<<maxDispatches<<"}\n";
}
template<class T> void* method(T* object,size_t index){return (*reinterpret_cast<void***>(object))[index];}
template<class T> bool hook(void* target,void* detour,T& original){auto r=MH_CreateHook(target,detour,reinterpret_cast<void**>(&original));return r==MH_OK;}
using ComputeFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_COMPUTE_PIPELINE_STATE_DESC*,REFIID,void**);
ComputeFn realCompute;
HRESULT STDMETHODCALLTYPE compute(ID3D12Device* d,const D3D12_COMPUTE_PIPELINE_STATE_DESC* p,REFIID iid,void** out){
    HRESULT r=realCompute(d,p,iid,out);
    if(SUCCEEDED(r)&&out&&*out)try{std::lock_guard guard(mutex);auto h=dump(p->CS.pShaderBytecode,p->CS.BytecodeLength,".dxbc");pipelines[*out]=h;inspectPass(h,p->CS.pShaderBytecode,p->CS.BytecodeLength);
        line("{\"event\":\"compute_pipeline\",\"pipeline\":\""+hex(uintptr_t(*out))+"\",\"shader\":\""+hex(h)+"\",\"root\":\""+hex(uintptr_t(p->pRootSignature))+"\"}");}catch(...){}
    return r;
}
using RootFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const void*,SIZE_T,REFIID,void**);
RootFn realRoot;
HRESULT STDMETHODCALLTYPE root(ID3D12Device* d,UINT node,const void* bytes,SIZE_T size,REFIID iid,void** out){
    HRESULT r=realRoot(d,node,bytes,size,iid,out);
    if(SUCCEEDED(r)&&out&&*out)try{
        std::lock_guard guard(mutex);Root rs;rs.hash=dump(bytes,size,".rootsig");
        ComPtr<ID3D12VersionedRootSignatureDeserializer> decoder;
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* versioned=nullptr;
        if(SUCCEEDED(D3D12CreateVersionedRootSignatureDeserializer(bytes,size,IID_PPV_ARGS(&decoder)))&&
           SUCCEEDED(decoder->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0,&versioned))){
            auto desc=&versioned->Desc_1_0;
            for(UINT i=0;i<desc->NumParameters;i++)if(desc->pParameters[i].ParameterType==D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE){
                auto table=desc->pParameters[i].DescriptorTable;UINT next=0;
                for(UINT j=0;j<table.NumDescriptorRanges;j++){auto range=table.pDescriptorRanges[j];UINT offset=range.OffsetInDescriptorsFromTableStart;
                    if(offset==D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)offset=next;
                    rs.ranges.push_back({i,offset,std::min(range.NumDescriptors,128u),range.BaseShaderRegister,range.RegisterSpace,range.RangeType,range.NumDescriptors});
                    next=offset+range.NumDescriptors;
                }
            }
        }
        roots[*out]=std::move(rs);line("{\"event\":\"root_signature\",\"root\":\""+hex(uintptr_t(*out))+"\",\"hash\":\""+hex(roots[*out].hash)+"\"}");
    }catch(...){}return r;
}
using SrvFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,const D3D12_SHADER_RESOURCE_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
using UavFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,ID3D12Resource*,ID3D12Resource*,const D3D12_UNORDERED_ACCESS_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
using CbvFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
SrvFn realSrv;UavFn realUav;CbvFn realCbv;
void STDMETHODCALLTYPE srv(ID3D12Device* d,ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC* v,D3D12_CPU_DESCRIPTOR_HANDLE h){
    realSrv(d,r,v,h);try{View entry;entry.resource=uintptr_t(r);entry.kind='S';if(r)entry.desc=r->GetDesc();entry.format=v?v->Format:entry.desc.Format;entry.explicitDesc=v!=nullptr;if(v)entry.srv=*v;
        std::lock_guard guard(mutex);views[h.ptr]=entry;}catch(...){}
}
void STDMETHODCALLTYPE uav(ID3D12Device* d,ID3D12Resource* r,ID3D12Resource* counter,const D3D12_UNORDERED_ACCESS_VIEW_DESC* v,D3D12_CPU_DESCRIPTOR_HANDLE h){
    realUav(d,r,counter,v,h);try{View entry;entry.resource=uintptr_t(r);entry.kind='U';if(r)entry.desc=r->GetDesc();entry.format=v?v->Format:entry.desc.Format;entry.explicitDesc=v!=nullptr;if(v)entry.uav=*v;entry.counter=uintptr_t(counter);
        std::lock_guard guard(mutex);views[h.ptr]=entry;}catch(...){}
}
void STDMETHODCALLTYPE cbv(ID3D12Device* d,const D3D12_CONSTANT_BUFFER_VIEW_DESC* v,D3D12_CPU_DESCRIPTOR_HANDLE h){
    realCbv(d,v,h);try{View entry;entry.kind='C';if(v){entry.address=v->BufferLocation;entry.size=v->SizeInBytes;}std::lock_guard guard(mutex);views[h.ptr]=entry;}catch(...){}
}
using CopySimpleFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_DESCRIPTOR_HEAP_TYPE);
CopySimpleFn realCopySimple;
void STDMETHODCALLTYPE copySimple(ID3D12Device* d,UINT count,D3D12_CPU_DESCRIPTOR_HANDLE dst,D3D12_CPU_DESCRIPTOR_HANDLE src,D3D12_DESCRIPTOR_HEAP_TYPE type){
    realCopySimple(d,count,dst,src,type);
    if(type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)return;
    try{UINT stride=d->GetDescriptorHandleIncrementSize(type);std::lock_guard guard(mutex);
        std::vector<View> temp;temp.reserve(count);
        for(UINT i=0;i<count;i++){auto it=views.find(src.ptr+SIZE_T(i)*stride);temp.push_back(it!=views.end()?it->second:View{});}
        for(UINT i=0;i<count;i++)views[dst.ptr+SIZE_T(i)*stride]=temp[i];
    }catch(...){}
}
using CopyFn=void(STDMETHODCALLTYPE*)(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
CopyFn realCopy;
void STDMETHODCALLTYPE copy(ID3D12Device* d,UINT nd,const D3D12_CPU_DESCRIPTOR_HANDLE* dst,const UINT* ds,UINT ns,const D3D12_CPU_DESCRIPTOR_HANDLE* src,const UINT* ss,D3D12_DESCRIPTOR_HEAP_TYPE type){
    realCopy(d,nd,dst,ds,ns,src,ss,type);if(type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)return;
    try{UINT stride=d->GetDescriptorHandleIncrementSize(type);std::lock_guard guard(mutex);std::vector<View> temp;
        for(UINT j=0;j<ns;j++)for(UINT i=0;i<(ss?ss[j]:1);i++){auto it=views.find(src[j].ptr+SIZE_T(i)*stride);temp.push_back(it!=views.end()?it->second:View{});}
        size_t k=0;for(UINT j=0;j<nd;j++)for(UINT i=0;i<(ds?ds[j]:1);i++)if(k<temp.size())views[dst[j].ptr+SIZE_T(i)*stride]=temp[k++];
    }catch(...){}
}
using ResetFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
ResetFn realReset;
HRESULT STDMETHODCALLTYPE reset(ID3D12GraphicsCommandList* l,ID3D12CommandAllocator* a,ID3D12PipelineState* p){
    auto r=realReset(l,a,p);if(SUCCEEDED(r))try{std::lock_guard guard(mutex);states[l]=State{};states[l].pipeline=p;if(p){auto it=pipelines.find(p);if(it!=pipelines.end())states[l].shader=it->second;}}catch(...){}return r;
}
using PipelineFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12PipelineState*);
PipelineFn realPipeline;
void STDMETHODCALLTYPE pipeline(ID3D12GraphicsCommandList* l,ID3D12PipelineState* p){realPipeline(l,p);try{std::lock_guard guard(mutex);auto it=pipelines.find(p);states[l].shader=it!=pipelines.end()?it->second:0;states[l].pipeline=p;}catch(...){} }
using SetRootFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12RootSignature*);
SetRootFn realSetRoot;
void STDMETHODCALLTYPE setRoot(ID3D12GraphicsCommandList* l,ID3D12RootSignature* r){realSetRoot(l,r);try{std::lock_guard guard(mutex);auto& s=states[l];if(s.signature!=uintptr_t(r)){s.tables={};s.cbvs={};for(auto& c:s.constants)c.clear();}s.signature=uintptr_t(r);}catch(...){} }
using HeapsFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,ID3D12DescriptorHeap*const*);
HeapsFn realHeaps;
void STDMETHODCALLTYPE heaps(ID3D12GraphicsCommandList* l,UINT count,ID3D12DescriptorHeap*const* h){realHeaps(l,count,h);try{std::vector<Heap> out;
    for(UINT i=0;i<count;i++){auto desc=h[i]->GetDesc();if(desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)continue;
        ComPtr<ID3D12Device> d;if(SUCCEEDED(h[i]->GetDevice(IID_PPV_ARGS(&d))))out.push_back({h[i]->GetGPUDescriptorHandleForHeapStart().ptr,h[i]->GetCPUDescriptorHandleForHeapStart().ptr,desc.NumDescriptors,d->GetDescriptorHandleIncrementSize(desc.Type)});}
    std::lock_guard guard(mutex);states[l].heaps=std::move(out);states[l].nativeHeaps.assign(h,h+count);
}catch(...){} }
using TableFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_DESCRIPTOR_HANDLE);
TableFn realTable;
void STDMETHODCALLTYPE table(ID3D12GraphicsCommandList* l,UINT index,D3D12_GPU_DESCRIPTOR_HANDLE h){realTable(l,index,h);if(index<64)try{std::lock_guard guard(mutex);states[l].tables[index]=h.ptr;}catch(...){} }
using RootCbvFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,D3D12_GPU_VIRTUAL_ADDRESS);
RootCbvFn realRootCbv;
void STDMETHODCALLTYPE rootCbv(ID3D12GraphicsCommandList* l,UINT index,D3D12_GPU_VIRTUAL_ADDRESS a){realRootCbv(l,index,a);if(index<64)try{std::lock_guard guard(mutex);states[l].cbvs[index]=a;}catch(...){} }
using ConstantsFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,const void*,UINT);
ConstantsFn realConstants;
void recordConstants(ID3D12GraphicsCommandList* l,UINT index,UINT count,const void* data,UINT offset){
    if(index>=64||offset>=256||count>256-offset||!data)return;std::lock_guard guard(mutex);auto& c=states[l].constants[index];if(c.size()<offset+count)c.resize(offset+count);memcpy(c.data()+offset,data,count*4);
}
void STDMETHODCALLTYPE constants(ID3D12GraphicsCommandList* l,UINT index,UINT count,const void* data,UINT offset){realConstants(l,index,count,data,offset);try{recordConstants(l,index,count,data,offset);}catch(...){} }
using ConstantFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT);
ConstantFn realConstant;
void STDMETHODCALLTYPE constant(ID3D12GraphicsCommandList* l,UINT index,UINT data,UINT offset){realConstant(l,index,data,offset);try{recordConstants(l,index,1,&data,offset);}catch(...){} }
using MapFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Resource*,UINT,const D3D12_RANGE*,void**);
using UnmapFn=void(STDMETHODCALLTYPE*)(ID3D12Resource*,UINT,const D3D12_RANGE*);
MapFn realMap;UnmapFn realUnmap;
HRESULT STDMETHODCALLTYPE map(ID3D12Resource* r,UINT sub,const D3D12_RANGE* range,void** data){auto hr=realMap(r,sub,range,data);
    if(SUCCEEDED(hr)&&data&&*data)try{auto desc=r->GetDesc();if(desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER){std::lock_guard guard(mutex);mappings[r]={r->GetGPUVirtualAddress(),desc.Width,*data};}}catch(...){}return hr;
}
void STDMETHODCALLTYPE unmap(ID3D12Resource* r,UINT sub,const D3D12_RANGE* range){try{std::lock_guard guard(mutex);mappings.erase(r);}catch(...){}realUnmap(r,sub,range);}
std::string cbvData(UINT64 address,UINT size){
    for(auto& [_,m]:mappings)if(address>=m.gpu&&address-m.gpu<m.size){size_t offset=size_t(address-m.gpu);size_t bytes=std::min({size_t(size),size_t(2048),size_t(m.size-offset)});
        auto ptr=static_cast<const char*>(m.cpu)+offset;MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(ptr,&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return "null";
        if(uintptr_t(ptr)+bytes>uintptr_t(info.BaseAddress)+info.RegionSize)return "null";
        std::ostringstream s;s<<'[';for(size_t i=0;i<bytes/4;i++){UINT value=0;memcpy(&value,ptr+i*4,4);if(i)s<<',';s<<value;}s<<']';return s.str();
    }return "null";
}
void captureDispatch(ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z,const char* kind){
    std::lock_guard guard(mutex);auto& s=states[list];
    // Repeated menu/NGX dispatches can otherwise exhaust the capture before the
    // world loads. Retain a small sample of each pipeline/root/group shape.
    auto key=std::string(kind)+":"+hex(s.shader)+":"+hex(s.signature)+":"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(z);
    auto& seen=occurrences[key];if(seen>=16)return;++seen;
    uint64_t n=++dispatchCount;if(n>maxDispatches)return;
    std::ostringstream o;
    o<<"{\"event\":\""<<kind<<"\",\"sequence\":"<<n<<",\"list\":\""<<hex(uintptr_t(list))<<"\",\"shader\":\""<<hex(s.shader)
        <<"\",\"root\":\""<<hex(s.signature)<<"\",\"groups\":["<<x<<','<<y<<','<<z<<"],\"views\":[";
    bool first=true;auto rs=roots.find(reinterpret_cast<void*>(s.signature));
    if(rs!=roots.end())for(auto& range:rs->second.ranges){if(range.root>=64)continue;auto gpu=s.tables[range.root];if(!gpu)continue;
        for(auto& heap:s.heaps){if(gpu<heap.gpu||gpu-heap.gpu>=UINT64(heap.count)*heap.stride)continue;
            for(UINT i=0;i<range.count;i++){UINT64 off=(gpu-heap.gpu)+UINT64(range.offset+i)*heap.stride;if(off>=UINT64(heap.count)*heap.stride)break;
                auto it=views.find(heap.cpu+SIZE_T(off));if(it==views.end())continue;auto& v=it->second;if(!first)o<<',';first=false;
                o<<"{\"root_index\":"<<range.root<<",\"table_offset\":"<<range.offset+i<<",\"register\":"<<range.reg+i<<",\"space\":"<<range.space
                    <<",\"kind\":\""<<v.kind<<"\",\"resource\":\""<<hex(v.resource)<<"\",\"width\":"<<v.desc.Width<<",\"height\":"<<v.desc.Height
                    <<",\"resource_format\":"<<unsigned(v.desc.Format)<<",\"view_format\":"<<unsigned(v.format);
                if(v.kind=='C')o<<",\"gpu_address\":\""<<hex(v.address)<<"\",\"bytes\":"<<v.size<<",\"mapped_u32\":"<<cbvData(v.address,v.size);
                auto transition=s.transitions.find(v.resource);
                if(transition!=s.transitions.end())o<<",\"recorded_transition_state\":"<<unsigned(transition->second);
                o<<'}';
            }break;
        }
    }
    o<<"],\"root_cbvs\":[";first=true;
    for(UINT i=0;i<64;i++)if(s.cbvs[i]){if(!first)o<<',';first=false;o<<"{\"root_index\":"<<i<<",\"gpu_address\":\""<<hex(s.cbvs[i])<<"\",\"mapped_u32\":"<<cbvData(s.cbvs[i],2048)<<'}';}
    o<<"],\"root_constants\":[";first=true;
    for(UINT i=0;i<64;i++)if(!s.constants[i].empty()){if(!first)o<<',';first=false;o<<"{\"root_index\":"<<i<<",\"u32\":[";for(size_t j=0;j<s.constants[i].size();j++){if(j)o<<',';o<<s.constants[i][j];}o<<"]}";}
    o<<"]}";line(o.str());if(n==maxDispatches)status("complete","Dispatch capture limit reached; restart game for another capture");
}
using DispatchFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,UINT,UINT);
using BarriersFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
BarriersFn realBarriers;
void STDMETHODCALLTYPE barriers(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* data){
    realBarriers(list,count,data);
    if(!data)return;
    try{std::lock_guard guard(mutex);auto& s=states[list];
        for(UINT i=0;i<count;i++){
            auto& b=data[i];
            if(b.Type==D3D12_RESOURCE_BARRIER_TYPE_ALIASING){
                // Aliasing invalidates previous assumptions, including resources
                // not explicitly named in a wildcard aliasing barrier.
                if(b.Aliasing.pResourceBefore&&b.Aliasing.pResourceAfter){for(auto resource:{b.Aliasing.pResourceBefore,b.Aliasing.pResourceAfter}){auto key=uintptr_t(resource);s.transitions.erase(key);s.invalidStates.insert(key);untouchedInitialStates.erase(key);}}
                else {s.transitions.clear();s.invalidatesInherited=true;untouchedInitialStates.clear();}
                continue;
            }
            if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)continue;
            auto key=uintptr_t(b.Transition.pResource);
            untouchedInitialStates.erase(key);
            if(b.Flags!=D3D12_RESOURCE_BARRIER_FLAG_NONE||b.Transition.Subresource!=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
                {s.transitions.erase(key);s.invalidStates.insert(key);}
            else {s.transitions[key]=b.Transition.StateAfter;s.invalidStates.erase(key);}
        }
    }catch(...){}
}
DispatchFn realDispatch;
void bridgePass(ID3D12GraphicsCommandList*,UINT,UINT,UINT,bool);
void STDMETHODCALLTYPE dispatch(ID3D12GraphicsCommandList* l,UINT x,UINT y,UINT z){try{captureDispatch(l,x,y,z,"dispatch");bridgePass(l,x,y,z,false);}catch(...){}realDispatch(l,x,y,z);try{bridgePass(l,x,y,z,true);}catch(...){} }
using RayPipelineFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,ID3D12StateObject*);
using RaysFn=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList4*,const D3D12_DISPATCH_RAYS_DESC*);
RayPipelineFn realRayPipeline;RaysFn realRays;
void STDMETHODCALLTYPE rayPipeline(ID3D12GraphicsCommandList4* l,ID3D12StateObject* p){realRayPipeline(l,p);try{std::lock_guard guard(mutex);auto it=pipelines.find(p);states[l].shader=it!=pipelines.end()?it->second:0;}catch(...){} }
void STDMETHODCALLTYPE rays(ID3D12GraphicsCommandList4* l,const D3D12_DISPATCH_RAYS_DESC* d){try{captureDispatch(l,d->Width,d->Height,d->Depth,"dispatch_rays");}catch(...){}realRays(l,d);try{bridgePass(l,d->Width,d->Height,d->Depth,true);}catch(...){} }
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
ExecuteFn realExecute;
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* lists){
    // A queue can be handed sequentially between CPU threads. Only overlapping
    // submissions make our post-call ordering evidence ambiguous. Never hold
    // the observer mutex across the driver's ExecuteCommandLists call.
    bool observing=changedRendererContract.load();
    if(observing)try{std::lock_guard guard(mutex);if(queueActiveCalls[q]++)ambiguousQueues.insert(q);}catch(...){observing=false;}
    realExecute(q,n,lists);
    if(observing)try{std::lock_guard guard(mutex);if(queueActiveCalls[q])--queueActiveCalls[q];
        for(UINT i=0;i<n;i++){listQueues[lists[i]]=q;auto it=states.find(lists[i]);if(it==states.end())continue;auto& s=it->second;
            if(s.invalidatesInherited)submittedStates.clear();for(auto resource:s.invalidStates)submittedStates.erase(resource);
            for(auto& [resource,state]:s.transitions){auto epoch=resourceEpoch(reinterpret_cast<ID3D12Resource*>(resource),true);if(epoch)submittedStates[resource]={state,epoch,q};}
        }
    }catch(...){}
    if(auto fn=bridgeQueue.load())try{fn(q,n,lists);}catch(...){}
}
void restoreState(ID3D12GraphicsCommandList* list,const State& saved){
    if(!saved.nativeHeaps.empty())realHeaps(list,UINT(saved.nativeHeaps.size()),saved.nativeHeaps.data());
    realSetRoot(list,reinterpret_cast<ID3D12RootSignature*>(saved.signature));
    if(saved.pipeline)realPipeline(list,saved.pipeline);
    for(UINT i=0;i<64;i++){
        if(saved.tables[i])realTable(list,i,{saved.tables[i]});
        if(saved.cbvs[i])realRootCbv(list,i,saved.cbvs[i]);
        if(!saved.constants[i].empty())realConstants(list,i,UINT(saved.constants[i].size()),saved.constants[i].data(),0);
    }
    std::lock_guard guard(mutex);states[list]=saved;
}
void bridgePass(ID3D12GraphicsCommandList* list,UINT x,UINT y,UINT z,bool after){
    auto callback=bridgeCallback.load();if(!callback||bridgeExecuting)return;
    State saved;std::vector<BridgeBinding> bound;std::array<unsigned char,1792> camera{};UINT cameraBytes=0;
    uint64_t identifiedShader=0;
    unsigned lightingSrvSpace=0;bool auxiliaryBuffer=false,auxiliaryViewKnown=false,auxiliaryCounter=false;D3D12_UNORDERED_ACCESS_VIEW_DESC auxiliaryView{};
    {
        std::lock_guard guard(mutex);saved=states[list];
        identifiedShader=saved.shader;
        auto evidence=passEvidence.find(saved.shader);
        if(!knownPass(saved.shader)){
            if(evidence==passEvidence.end()||evidence->second.candidate.role==shader_contract::Role::none)return;
            auto& e=evidence->second;
            if(!e.candidate.compatible){if(!e.rejectedLogged){e.rejectedLogged=true;line("{\"event\":\"pass_contract_rejected\",\"shader\":\""+hex(saved.shader)+"\",\"reason\":\""+e.candidate.reason+"\",\"rr_fallback\":true}");}return;}
            identifiedShader=shader_contract::canonical(e.candidate.role);
            lightingSrvSpace=e.candidate.lightingSrvSpace;auxiliaryBuffer=e.candidate.auxiliaryBuffer;
        }
        bool raw=identifiedShader==0xe91e519842717ad4ull||identifiedShader==0x17e950ce2bdc6a38ull||identifiedShader==0x7905c7545a3c35b3ull;
        if((raw&&!after)||(!raw&&after))return;
        auto rs=roots.find(reinterpret_cast<void*>(saved.signature));if(rs==roots.end())return;
        for(auto range:rs->second.ranges)for(auto heap:saved.heaps){auto gpu=saved.tables[range.root];if(!gpu||gpu<heap.gpu||gpu-heap.gpu>=UINT64(heap.count)*heap.stride)continue;
            for(UINT i=0;i<range.count;i++){auto offset=gpu-heap.gpu+UINT64(range.offset+i)*heap.stride;if(offset>=UINT64(heap.count)*heap.stride)break;
                auto it=views.find(heap.cpu+SIZE_T(offset));if(it==views.end())continue;auto& v=it->second;
                auto st=saved.transitions.find(v.resource);
                if(auxiliaryBuffer&&range.space==14&&range.reg+i==0&&v.kind=='U'){auxiliaryView=v.uav;auxiliaryViewKnown=v.explicitDesc;auxiliaryCounter=v.counter!=0;}
                bound.push_back({range.root,range.offset+i,range.reg+i,range.space,v.kind,reinterpret_cast<ID3D12Resource*>(v.resource),v.format,st==saved.transitions.end()?D3D12_RESOURCE_STATE_COMMON:st->second,st!=saved.transitions.end()});
                // Root/descriptor CBVs have no resource pointer, and unused
                // bindless entries can outlive their resources. Query only
                // the small set consumed by this pass's runtime contract.
                UINT reg=range.reg+i;
                UINT diffuseIndex=saved.constants[0].empty()?UINT_MAX:saved.constants[0][0]&255,specularIndex=saved.constants[0].empty()?UINT_MAX:(saved.constants[0][0]>>8)&255;
                bool contractResource=v.resource&&((auxiliaryBuffer&&range.space==14&&v.kind=='U'&&reg==0)||
                    (lightingSrvSpace&&range.space==lightingSrvSpace&&v.kind=='S'&&(reg==diffuseIndex||reg==specularIndex||reg==diffuseIndex+8))||
                    (range.space==0&&
                    ((raw&&v.kind=='U'&&(reg==5||reg==6||reg==15||reg==28))||
                     (!raw&&((v.kind=='S'&&(reg==14||reg==15||reg==16||reg==19||reg==21||reg==44||reg==46))||
                             (v.kind=='U'&&(reg==12||reg==14||reg==16||reg==17||reg==18||reg==23)))))));
                if(!knownPass(saved.shader)&&!raw&&v.kind=='U'&&range.space==0&&reg==14&&!shader_contract::has(evidence->second.profile,6,14))contractResource=false;
                if(!knownPass(saved.shader)&&contractResource&&st==saved.transitions.end()){D3D12_RESOURCE_STATES inherited{};if(inheritedState(list,reinterpret_cast<ID3D12Resource*>(v.resource),inherited)){bound.back().state=inherited;bound.back().stateKnown=true;}}
                if(v.kind=='C'&&range.reg+i==0&&range.space==0){for(auto& [_,m]:mappings)if(v.address>=m.gpu&&v.address-m.gpu+camera.size()<=m.size){
                    auto ptr=static_cast<unsigned char*>(m.cpu)+(v.address-m.gpu);MEMORY_BASIC_INFORMATION info{};
                    if(VirtualQuery(ptr,&info,sizeof(info))&&info.State==MEM_COMMIT&&!(info.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&uintptr_t(ptr)+camera.size()<=uintptr_t(info.BaseAddress)+info.RegionSize){memcpy(camera.data(),ptr,camera.size());cameraBytes=UINT(camera.size());}break;}}
            }break;
        }
        if(knownPass(saved.shader)&&cameraBytes==camera.size()){
            UINT frame=0;memcpy(&frame,camera.data()+1352,4);
            unsigned role=identifiedShader==0xa2c049a5a3f59ee4ull?3:identifiedShader==0xe91e519842717ad4ull?2:1;
            validatedRoleFrame[role]=frame;
        }
        if(!knownPass(saved.shader)){
            auto& e=evidence->second;
            if(e.invalidLogs<3&&raw)for(auto& b:bound)if(b.space==0&&b.kind=='U'&&(b.reg==6||b.reg==15)&&b.resource&&!b.stateKnown){auto key=uintptr_t(b.resource);auto q=listQueues.find(list);line("{\"event\":\"pass_state_evidence\",\"shader\":\""+hex(saved.shader)+"\",\"register\":"+std::to_string(b.reg)+",\"resource\":\""+hex(key)+"\",\"initial_recorded\":"+(untouchedInitialStates.contains(key)?"true":"false")+",\"submitted_recorded\":"+(submittedStates.contains(key)?"true":"false")+",\"list_queue_known\":"+(q!=listQueues.end()?"true":"false")+",\"queue_ambiguous\":"+(q!=listQueues.end()&&ambiguousQueues.contains(q->second)?"true":"false")+",\"inheritance_invalidated\":"+(saved.invalidatesInherited||saved.invalidStates.contains(key)?"true":"false")+"}");}
            float w=0,h=0;UINT frame=0,method=0,taa=0;
            memcpy(&w,camera.data()+1248,4);memcpy(&h,camera.data()+1252,4);
            memcpy(&frame,camera.data()+1352,4);memcpy(&method,camera.data()+1460,4);memcpy(&taa,camera.data()+1532,4);
            bool valid=cameraBytes==camera.size()&&w>=16&&h>=16&&w<=8192&&h<=8192&&method==0&&taa==0;
            auto checkTexture=[&](char kind,UINT reg,DXGI_FORMAT format,UINT space=0){
                for(auto& b:bound)if(b.kind==kind&&b.reg==reg&&b.space==space&&b.resource){
                    auto d=b.resource->GetDesc();
                    return b.format==format&&d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&
                        d.Width>=w&&d.Width<=w+1&&d.Height>=h&&d.Height<=h+1&&b.stateKnown&&
                        (kind=='U'?(b.state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS||
                          ((reg==15||(e.candidate.role==shader_contract::Role::combine&&reg==18&&space==0))&&
                           (b.state==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE||b.state==(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)))):
                        ((b.state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)!=0||
                         (space==lightingSrvSpace&&lightingSrvSpace&&b.state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS)));
                }return false;
            };
            if(e.candidate.role==shader_contract::Role::diffuse)
                valid=valid&&checkTexture('U',5,DXGI_FORMAT_R16G16B16A16_FLOAT)&&checkTexture('U',28,DXGI_FORMAT_R16G16B16A16_FLOAT);
            else if(e.candidate.role==shader_contract::Role::specular)
                valid=valid&&checkTexture('U',6,DXGI_FORMAT_R16G16B16A16_FLOAT)&&checkTexture('U',15,DXGI_FORMAT_R16_FLOAT);
            else valid=valid&&checkTexture('S',14,DXGI_FORMAT_R32_FLOAT)&&checkTexture('S',21,DXGI_FORMAT_R16G16_FLOAT)&&checkTexture('U',18,DXGI_FORMAT_R16G16B16A16_FLOAT);
            if(lightingSrvSpace){UINT diffuse=saved.constants[0].empty()?UINT_MAX:saved.constants[0][0]&255,specular=saved.constants[0].empty()?UINT_MAX:(saved.constants[0][0]>>8)&255;valid=valid&&diffuse<4&&specular<8&&diffuse!=specular&&checkTexture('S',diffuse,DXGI_FORMAT_R16G16B16A16_FLOAT,lightingSrvSpace)&&checkTexture('S',specular,DXGI_FORMAT_R16G16B16A16_FLOAT,lightingSrvSpace)&&checkTexture('S',diffuse+8,DXGI_FORMAT_R16G16B16A16_FLOAT,lightingSrvSpace);}
            if(auxiliaryBuffer){bool auxiliaryValid=false;for(auto& b:bound)if(b.kind=='U'&&b.space==14&&b.reg==0&&b.resource&&b.stateKnown){auto desc=b.resource->GetDesc();auxiliaryValid=auxiliaryViewKnown&&!auxiliaryCounter&&auxiliaryView.ViewDimension==D3D12_UAV_DIMENSION_BUFFER&&auxiliaryView.Format==DXGI_FORMAT_UNKNOWN&&auxiliaryView.Buffer.StructureByteStride&&auxiliaryView.Buffer.NumElements&&auxiliaryView.Buffer.Flags==D3D12_BUFFER_UAV_FLAG_NONE&&desc.Dimension==D3D12_RESOURCE_DIMENSION_BUFFER&&desc.Width<=1024*1024&&(b.state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS||(b.state&D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)!=0);}valid=valid&&auxiliaryValid;}
            if(!valid&&e.candidate.role==shader_contract::Role::combine&&(e.invalidLogs==32||e.invalidLogs==128)){
                std::ostringstream detail;detail<<"{\"event\":\"combine_runtime_contract_evidence\",\"frame\":"<<frame<<",\"render\":["<<w<<','<<h<<"],\"camera_bytes\":"<<cameraBytes<<",\"method\":"<<method<<",\"taa\":"<<taa<<",\"constants\":"<<(saved.constants[0].empty()?0:saved.constants[0][0])<<",\"aux_view_known\":"<<(auxiliaryViewKnown?"true":"false")<<",\"aux_counter\":"<<(auxiliaryCounter?"true":"false")<<",\"aux_stride\":"<<auxiliaryView.Buffer.StructureByteStride<<",\"aux_elements\":"<<auxiliaryView.Buffer.NumElements<<",\"bindings\":[";bool first=true;
                for(auto& b:bound){bool used=(b.space==0&&((b.kind=='S'&&(b.reg==14||b.reg==21))||(b.kind=='U'&&b.reg==18)))||(lightingSrvSpace&&b.space==lightingSrvSpace&&b.kind=='S'&&(b.reg==0||b.reg==4||b.reg==8))||(auxiliaryBuffer&&b.space==14&&b.reg==0&&b.kind=='U');if(!used)continue;if(!first)detail<<',';first=false;auto desc=b.resource?b.resource->GetDesc():D3D12_RESOURCE_DESC{};detail<<"{\"kind\":\""<<b.kind<<"\",\"register\":"<<b.reg<<",\"space\":"<<b.space<<",\"format\":"<<unsigned(b.format)<<",\"width\":"<<desc.Width<<",\"height\":"<<desc.Height<<",\"state_known\":"<<(b.stateKnown?"true":"false")<<",\"state\":"<<unsigned(b.state)<<'}';}detail<<"]}";line(detail.str());
            }
            if(!valid){if(e.invalidLogs++<3){std::ostringstream detail;detail<<"{\"event\":\"pass_runtime_contract_rejected\",\"shader\":\""<<hex(saved.shader)<<"\",\"frame\":"<<frame<<",\"camera_bytes\":"<<cameraBytes<<",\"render_method\":"<<method<<",\"taa\":"<<taa<<",\"resources\":[";bool first=true;for(auto& b:bound)if(b.space==0&&b.kind=='U'&&(b.reg==5||b.reg==6||b.reg==15||b.reg==28)){if(!first)detail<<',';first=false;detail<<"{\"register\":"<<b.reg<<",\"resource\":\""<<hex(uintptr_t(b.resource))<<"\",\"format\":"<<unsigned(b.format)<<",\"state_known\":"<<(b.stateKnown?"true":"false")<<",\"state\":"<<unsigned(b.state)<<'}';}detail<<"]}";line(detail.str());}if(e.accepted)line("{\"event\":\"pass_contract_revoked\",\"shader\":\""+hex(saved.shader)+"\",\"frame\":"+std::to_string(frame)+"}");e.frames=0;e.lastFrame=UINT_MAX;e.accepted=false;return;}
            auto role=unsigned(e.candidate.role);
            if(candidateRoleFrame[role]==frame&&candidateRoleHash[role]!=saved.shader){
                auto previous=passEvidence.find(candidateRoleHash[role]);
                if(previous!=passEvidence.end()){previous->second.accepted=false;previous->second.frames=0;}
                e.accepted=false;e.frames=0;validatedRoleFrame[role]=UINT_MAX;
                if(candidateRoleHash[role])line("{\"event\":\"pass_contract_ambiguous\",\"role\":"+std::to_string(role)+",\"frame\":"+std::to_string(frame)+",\"first_shader\":\""+hex(candidateRoleHash[role])+"\",\"second_shader\":\""+hex(saved.shader)+"\",\"rr_fallback\":true}");
                candidateRoleHash[role]=0;return;
            }
            candidateRoleFrame[role]=frame;candidateRoleHash[role]=saved.shader;
            if(e.lastFrame!=frame){e.lastFrame=frame;++e.frames;}
            unsigned required=e.cached?4:12;
            if(!e.accepted&&e.frames>=required){e.accepted=true;
                auto fp=shader_contract::fingerprint(e.profile);advisoryCache[saved.shader]=fp;
                std::ofstream cache(folder/L"pass-fingerprints-v1.txt",std::ios::trunc);
                unsigned entries=0;for(auto& [hash,signature]:advisoryCache){if(entries++==256)break;cache<<std::hex<<hash<<' '<<signature<<'\n';}
                line("{\"event\":\"pass_contract_validated\",\"shader\":\""+hex(saved.shader)+"\",\"canonical_role\":\""+hex(identifiedShader)+"\",\"validated_frames\":"+std::to_string(e.frames)+",\"frame\":"+std::to_string(frame)+"}");
            }
            if(!e.accepted)return;
            validatedRoleFrame[unsigned(e.candidate.role)]=frame;
            // Do not activate half a reconstruction graph. Raw candidates can
            // be cached independently, but all three boundaries must be live
            // and compatible before any unknown shader records bridge work.
            for(unsigned role=1;role<4;role++)if(validatedRoleFrame[role]==UINT_MAX||
                unsigned(frame-validatedRoleFrame[role])>12)return;
            // Bridge descriptor cloning reads the same canonical contract.
            // Keep the actual PSO and restore its observed hash after callback.
            states[list].shader=identifiedShader;
        }
    }
    BridgePass pass{list,identifiedShader,saved.pipeline,reinterpret_cast<ID3D12RootSignature*>(saved.signature),bound.data(),UINT(bound.size()),camera.data(),cameraBytes,{},{x,y,z},after,saved.shader};
    pass.lightingSrvSpace=lightingSrvSpace;pass.auxiliaryBuffer=auxiliaryBuffer;pass.auxiliaryViewKnown=auxiliaryViewKnown;pass.auxiliaryCounter=auxiliaryCounter;pass.auxiliaryView=auxiliaryView;
    if(!knownPass(saved.shader)){std::lock_guard guard(mutex);auto e=passEvidence.find(saved.shader);pass.extraOutput14=e!=passEvidence.end()&&shader_contract::has(e->second.profile,6,14);}
    for(size_t i=0;i<std::min(size_t(3),saved.constants[0].size());i++)pass.constants[i]=saved.constants[0][i];
    bridgeCamera=camera;bridgeLightingSrvSpace=lightingSrvSpace;bridgeAuxiliaryBuffer=auxiliaryBuffer;bridgeExtraOutput14=pass.extraOutput14;bridgeExecuting=true;try{callback(&pass);}catch(...){}
    if(identifiedShader==0xa2c049a5a3f59ee4ull)restoreState(list,saved);
    else {std::lock_guard guard(mutex);states[list]=saved;}
    bridgeExecuting=false;bridgeLightingSrvSpace=0;bridgeAuxiliaryBuffer=false;
}
bool cloneTables(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* target,UINT* offsets,UINT* counts){
    if(!list||!target||!offsets||!counts)return false;
    struct Definition {UINT index;View view;D3D12_DESCRIPTOR_RANGE_TYPE type;};std::vector<Definition> definitions;
    UINT total=0;auto hd=target->GetDesc();if(hd.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)return false;
    {
        std::lock_guard guard(mutex);auto& state=states[list];auto root=roots.find(reinterpret_cast<void*>(state.signature));if(root==roots.end())return false;
        for(UINT i=0;i<64;i++){offsets[i]=UINT_MAX;counts[i]=0;}
        auto unusedRange=[&](const Range& range){return state.shader==0xa2c049a5a3f59ee4ull&&range.root==3;};
        // The native root has a 36,864-entry material table at root 3.
        // DXIL for this exact combine shader has no references to that table;
        // its sky texture uses root 4, space 6. Do not clone unused material
        // descriptors or reject their offsets against this private heap.
        for(auto range:root->second.ranges){if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER||unusedRange(range))continue;
            if(range.actualCount>8192||range.offset>8192-range.actualCount)return false;counts[range.root]=std::max(counts[range.root],range.offset+range.actualCount);}
        for(UINT i=0;i<64;i++)if(counts[i]&&state.tables[i]){offsets[i]=total;total+=counts[i];if(total>hd.NumDescriptors)return false;}
        for(auto range:root->second.ranges){if(unusedRange(range)||offsets[range.root]==UINT_MAX||range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)continue;auto gpu=state.tables[range.root];
            for(auto heap:state.heaps){if(gpu<heap.gpu||gpu-heap.gpu>=UINT64(heap.count)*heap.stride)continue;
                for(UINT i=0;i<range.actualCount;i++){UINT64 delta=gpu-heap.gpu+UINT64(range.offset+i)*heap.stride;if(delta>=UINT64(heap.count)*heap.stride)return false;
                    auto found=views.find(heap.cpu+SIZE_T(delta));View v=found==views.end()?View{}:found->second;
                    if(state.shader==0xa2c049a5a3f59ee4ull){
                        // Clone only descriptors actually referenced by this
                        // verified shader. Unused bindless entries may retain
                        // stale resource pointers after the game frees them.
                        bool used=true;UINT reg=range.reg+i;
                        if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV&&range.space==6){UINT sky=0,missing=0;memcpy(&sky,bridgeCamera.data()+1192,4);memcpy(&missing,bridgeCamera.data()+1580,4);used=reg==(sky<4096?sky:missing);}
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV&&range.space==0){used=false;for(UINT n:{11u,12u,14u,15u,16u,17u,19u,21u,38u,44u,46u,47u,50u,59u})if(reg==n)used=true;}
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV&&bridgeLightingSrvSpace&&range.space==bridgeLightingSrvSpace){UINT diffuse=state.constants[0].empty()?UINT_MAX:state.constants[0][0]&255;UINT specular=state.constants[0].empty()?UINT_MAX:(state.constants[0][0]>>8)&255;used=reg==diffuse||reg==specular||reg==diffuse+8;}
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV&&range.space!=4)used=false;
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_UAV&&range.space==0){used=false;for(UINT n:{12u,14u,16u,17u,18u,23u})if(reg==n)used=true;if(bridgeLightingSrvSpace&&reg==14&&!bridgeExtraOutput14)used=false;}
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_UAV&&bridgeAuxiliaryBuffer&&range.space==14)used=reg==0;
                        else if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_UAV&&range.space!=3)used=false;
                        if(!used&&range.type!=D3D12_DESCRIPTOR_RANGE_TYPE_CBV)v=View{};
                    }
                    if(range.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV&&v.kind!='C')return false;
                    definitions.push_back({offsets[range.root]+range.offset+i,v,range.type});}break;}
        }
    }
    ComPtr<ID3D12Device> d;if(FAILED(target->GetDevice(IID_PPV_ARGS(&d))))return false;auto stride=d->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=target->GetCPUDescriptorHandleForHeapStart();
    for(auto& definition:definitions){auto h=cpu;h.ptr+=SIZE_T(definition.index)*stride;auto& v=definition.view;auto resource=reinterpret_cast<ID3D12Resource*>(v.resource);
        if(definition.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV){D3D12_CONSTANT_BUFFER_VIEW_DESC cb{v.address,v.size};d->CreateConstantBufferView(&cb,h);}
        else if(definition.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV){if(v.kind=='S')d->CreateShaderResourceView(resource,v.explicitDesc?&v.srv:nullptr,h);
            else{D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};nullView.Format=DXGI_FORMAT_R8G8B8A8_UNORM;nullView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;nullView.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;nullView.Texture2D.MipLevels=1;d->CreateShaderResourceView(nullptr,&nullView,h);}}
        else if(definition.type==D3D12_DESCRIPTOR_RANGE_TYPE_UAV){if(v.kind=='U')d->CreateUnorderedAccessView(resource,reinterpret_cast<ID3D12Resource*>(v.counter),v.explicitDesc?&v.uav:nullptr,h);
            else{D3D12_UNORDERED_ACCESS_VIEW_DESC nullView{};nullView.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;nullView.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(nullptr,nullptr,&nullView,h);}}
    }
    return true;
}
using StateObjectFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device5*,const D3D12_STATE_OBJECT_DESC*,REFIID,void**);
StateObjectFn realStateObject;
HRESULT STDMETHODCALLTYPE stateObject(ID3D12Device5* d,const D3D12_STATE_OBJECT_DESC* desc,REFIID iid,void** out){auto hr=realStateObject(d,desc,iid,out);
    if(SUCCEEDED(hr)&&out&&*out)try{std::lock_guard guard(mutex);uint64_t key=0;std::ostringstream o;o<<"{\"event\":\"ray_pipeline\",\"pipeline\":\""<<hex(uintptr_t(*out))<<"\",\"libraries\":[";bool first=true;
        for(UINT i=0;i<desc->NumSubobjects;i++)if(desc->pSubobjects[i].Type==D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY){auto lib=static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
            auto h=dump(lib->DXILLibrary.pShaderBytecode,lib->DXILLibrary.BytecodeLength,".dxil");if(!first)o<<',';first=false;o<<'"'<<hex(h)<<'"';key^=h;}
        pipelines[*out]=key;o<<"],\"shader\":\""<<hex(key)<<"\"}";line(o.str());
    }catch(...){}return hr;
}
// Stream PSOs are used by newer RenderDragon versions. Parse their documented
// aligned subobjects instead of assuming CreateComputePipelineState is used.
template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE K,class T>struct alignas(void*) Sub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;T value; };
using StreamFn=HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*,const D3D12_PIPELINE_STATE_STREAM_DESC*,REFIID,void**);
StreamFn realStream;
HRESULT STDMETHODCALLTYPE stream(ID3D12Device2* d,const D3D12_PIPELINE_STATE_STREAM_DESC* desc,REFIID iid,void** out){auto hr=realStream(d,desc,iid,out);
    if(SUCCEEDED(hr)&&out&&*out)try{std::lock_guard guard(mutex);auto bytes=static_cast<const unsigned char*>(desc->pPipelineStateSubobjectStream);size_t offset=0;uint64_t key=0;bool parsed=true;
        while(offset<desc->SizeInBytes){if(desc->SizeInBytes-offset<sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)){parsed=false;break;}auto type=*reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(bytes+offset);size_t size=0;
#define CASE_SUB(K,T) case K: size=sizeof(Sub<K,T>);break
            switch(type){
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:{size=sizeof(Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,D3D12_SHADER_BYTECODE>);if(offset+size<=desc->SizeInBytes){auto& s=reinterpret_cast<const Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,D3D12_SHADER_BYTECODE>*>(bytes+offset)->value;key=dump(s.pShaderBytecode,s.BytecodeLength,".dxbc");inspectPass(key,s.pShaderBytecode,s.BytecodeLength);}break;}
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,ID3D12RootSignature*);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,D3D12_SHADER_BYTECODE);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,D3D12_SHADER_BYTECODE);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS,D3D12_SHADER_BYTECODE);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS,D3D12_SHADER_BYTECODE);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS,D3D12_SHADER_BYTECODE);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS,D3D12_SHADER_BYTECODE);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,D3D12_SHADER_BYTECODE);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT,D3D12_STREAM_OUTPUT_DESC);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,D3D12_BLEND_DESC);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,UINT);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,D3D12_RASTERIZER_DESC);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,D3D12_DEPTH_STENCIL_DESC);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1,D3D12_DEPTH_STENCIL_DESC1);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,D3D12_INPUT_LAYOUT_DESC);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,D3D12_PRIMITIVE_TOPOLOGY_TYPE);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,D3D12_RT_FORMAT_ARRAY);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,DXGI_FORMAT);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,DXGI_SAMPLE_DESC);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK,UINT);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO,D3D12_CACHED_PIPELINE_STATE);
            CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,D3D12_PIPELINE_STATE_FLAGS);CASE_SUB(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,D3D12_VIEW_INSTANCING_DESC);
            default:parsed=false;break;
            }
#undef CASE_SUB
            if(!parsed||!size||offset+size>desc->SizeInBytes){parsed=false;break;}offset+=size;
        }
        if(key)pipelines[*out]=key;
        line("{\"event\":\"stream_pipeline\",\"pipeline\":\""+hex(uintptr_t(*out))+"\",\"shader\":\""+hex(key)+"\",\"parsed\":"+(parsed?"true":"false")+"}");
    }catch(...){}return hr;
}
DWORD WINAPI start(void*){
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    auto name=std::filesystem::path(image).filename();
    if(_wcsicmp(name.c_str(),L"Minecraft.Windows.exe")&&_wcsicmp(name.c_str(),L"rr_capture_test.exe"))return 0;
    auto loader=GetModuleHandleW(L"bedrock_rr_loader.dll");
    if(!loader)loader=GetModuleHandleW(L"dxgi.dll");
    auto bypass=reinterpret_cast<void(WINAPI*)(BOOL)>(GetProcAddress(loader,"BedrockRrBypassForThread"));
    if(bypass)bypass(TRUE);
    struct BypassGuard {void(WINAPI* fn)(BOOL);~BypassGuard(){if(fn)fn(FALSE);}} bypassGuard{bypass};
    try{
        wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
        folder=std::filesystem::path(path).parent_path()/L"capture";std::filesystem::create_directories(folder/L"shaders");
        logFile.open(folder/L"events.jsonl",std::ios::trunc);status("initializing","Installing native D3D12 observation hooks");
        ComPtr<IDXGIFactory6> factory;if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)))){status("failed","DXGI factory creation failed");return 1;}
        ComPtr<ID3D12Device> device;ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
        if(!device){status("failed","NVIDIA D3D12 device unavailable");return 1;}
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))){status("failed","Hook discovery command list creation failed");return 1;}
        if(MH_Initialize()!=MH_OK){status("failed","MinHook initialization failed");return 1;}
        bool ok=true;
        const auto& slots=*BedrockRrHookSlots();
#define HOOK(obj,index,fn,orig) ok=hook(method(obj,index),reinterpret_cast<void*>(&fn),orig)&&ok
        HOOK(device.Get(),slots.compute,compute,realCompute);HOOK(device.Get(),slots.root,root,realRoot);
        HOOK(device.Get(),slots.cbv,cbv,realCbv);HOOK(device.Get(),slots.srv,srv,realSrv);HOOK(device.Get(),slots.uav,uav,realUav);
        HOOK(device.Get(),slots.copy,copy,realCopy);HOOK(device.Get(),slots.copySimple,copySimple,realCopySimple);
        HOOK(list.Get(),slots.reset,reset,realReset);HOOK(list.Get(),slots.dispatch,dispatch,realDispatch);HOOK(list.Get(),slots.pipeline,pipeline,realPipeline);
        HOOK(list.Get(),slots.barriers,barriers,realBarriers);
        HOOK(device.Get(),slots.committed,committed,realCommitted);HOOK(device.Get(),slots.placed,placed,realPlaced);
        ComPtr<ID3D12Device4> d4;if(SUCCEEDED(device.As(&d4))){HOOK(d4.Get(),slots.committed1,committed1,realCommitted1);}
        ComPtr<ID3D12Device8> d8;if(SUCCEEDED(device.As(&d8))){HOOK(d8.Get(),slots.committed2,committed2,realCommitted2);HOOK(d8.Get(),slots.placed1,placed1,realPlaced1);}
        ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};
        if(SUCCEEDED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)))){HOOK(queue.Get(),slots.execute,execute,realExecute);}else ok=false;
        HOOK(list.Get(),slots.heaps,heaps,realHeaps);HOOK(list.Get(),slots.setRoot,setRoot,realSetRoot);HOOK(list.Get(),slots.table,table,realTable);
        HOOK(list.Get(),slots.constant,constant,realConstant);HOOK(list.Get(),slots.constants,constants,realConstants);HOOK(list.Get(),slots.rootCbv,rootCbv,realRootCbv);
        ComPtr<ID3D12Device2> d2;if(SUCCEEDED(device.As(&d2))){HOOK(d2.Get(),slots.stream,stream,realStream);}
        ComPtr<ID3D12Device5> d5;if(SUCCEEDED(device.As(&d5))){HOOK(d5.Get(),slots.stateObject,stateObject,realStateObject);}
        ComPtr<ID3D12GraphicsCommandList4> l4;if(SUCCEEDED(list.As(&l4))){HOOK(l4.Get(),slots.rayPipeline,rayPipeline,realRayPipeline);HOOK(l4.Get(),slots.rays,rays,realRays);}
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=256;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> upload;
        if(SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)))){HOOK(upload.Get(),slots.map,map,realMap);HOOK(upload.Get(),slots.unmap,unmap,realUnmap);}
#undef HOOK
        if(!ok||MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){MH_DisableHook(MH_ALL_HOOKS);status("failed","One or more native hooks failed; no renderer changes applied");return 1;}
        line("{\"event\":\"capture_started\",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"mode\":\"capture-only\",\"rr_evaluations\":0}");
        status("ready","Native hooks active; enter an RTX world with upscaling enabled");
        captureReady=true;SetEvent(captureReadyEvent);
        // Do not unload: active detours must stay valid until process exit.
        return 0;
    }catch(...){return 1;}
}
}
extern "C" __declspec(dllexport) BOOL WINAPI BedrockRrWaitUntilReady(DWORD timeout){return WaitForSingleObject(captureReadyEvent,timeout)==WAIT_OBJECT_0&&captureReady.load();}
extern "C" __declspec(dllexport) void BedrockRrRegisterBridge(BridgePassCallback pass,BridgeQueueCallback queue){bridgeQueue=queue;bridgeCallback=pass;}
extern "C" __declspec(dllexport) bool BedrockRrCloneTables(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* target,UINT* offsets,UINT* counts){return cloneTables(list,target,offsets,counts);}
extern "C" __declspec(dllexport) bool BedrockRrResourceState(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES* state){std::lock_guard guard(mutex);if(!state||!resource)return false;auto found=states[list].transitions.find(uintptr_t(resource));if(found!=states[list].transitions.end()){*state=found->second;return true;}return changedRendererContract.load()&&inheritedState(list,resource,*state);}
extern "C" __declspec(dllexport) void BedrockRrBindClone(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,const UINT* offsets){
    State saved;{std::lock_guard guard(mutex);saved=states[list];}
    std::vector<ID3D12DescriptorHeap*> heaps{heap};for(auto h:saved.nativeHeaps)if(h->GetDesc().Type==D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)heaps.push_back(h);
    realHeaps(list,UINT(heaps.size()),heaps.data());realSetRoot(list,reinterpret_cast<ID3D12RootSignature*>(saved.signature));if(saved.pipeline)realPipeline(list,saved.pipeline);
    ComPtr<ID3D12Device> d;heap->GetDevice(IID_PPV_ARGS(&d));UINT stride=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(UINT i=0;i<64;i++){if(saved.tables[i])realTable(list,i,{offsets[i]==UINT_MAX?saved.tables[i]:heap->GetGPUDescriptorHandleForHeapStart().ptr+UINT64(offsets[i])*stride});
        if(saved.cbvs[i])realRootCbv(list,i,saved.cbvs[i]);if(!saved.constants[i].empty())realConstants(list,i,UINT(saved.constants[i].size()),saved.constants[i].data(),0);}
}
extern "C" __declspec(dllexport) void BedrockRrPreserveCompute(ID3D12GraphicsCommandList* list,void(*fn)(void*),void* data){State saved;{std::lock_guard guard(mutex);saved=states[list];}bool previous=bridgeExecuting;bridgeExecuting=true;try{fn(data);}catch(...){}restoreState(list,saved);bridgeExecuting=previous;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=instance;captureReadyEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DisableThreadLibraryCalls(instance);HANDLE thread=CreateThread(nullptr,0,start,nullptr,0,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
