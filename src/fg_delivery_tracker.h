#pragma once
#include <unordered_map>
#include <vector>
#include <mutex>
#include <fstream>
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_defs_dlssg.h"
#include "delivery_telemetry.h"
// Passive identification only. Scheduling, fence policy and image ownership
// remain entirely with NVIDIA. Display time still comes from ETW, not Present.
namespace fg_delivery {
enum Type {Unknown,Real,Generated};
std::mutex mutex;std::unordered_map<ID3D12Resource*,Type> types;
struct Copy {ID3D12Resource* destination;ID3D12Resource* source;};
std::unordered_map<ID3D12CommandList*,std::vector<Copy>> copies;
std::ofstream trace;unsigned observedEvaluations=0;
std::atomic<bool> featureEnabled{false};
std::atomic<unsigned> physicalCalls{0};
using NativePresent=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using NativePresent1=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using NativeCopy=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12Resource*,ID3D12Resource*);
using NativeCopyTexture=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,const D3D12_TEXTURE_COPY_LOCATION*,UINT,UINT,UINT,const D3D12_TEXTURE_COPY_LOCATION*,const D3D12_BOX*);
using NativeExecute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using NativeReset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
NativeReset originalReset=nullptr;
NativePresent originalPresent=nullptr;NativePresent1 originalPresent1=nullptr;NativeCopy originalCopy=nullptr;NativeCopyTexture originalCopyTexture=nullptr;NativeExecute originalExecute=nullptr;
void remember(ID3D12GraphicsCommandList* list,ID3D12Resource* dest,ID3D12Resource* src){std::lock_guard guard(mutex);copies[list].push_back({dest,src});}
HRESULT STDMETHODCALLTYPE reset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,ID3D12PipelineState* pipeline){auto r=originalReset(list,allocator,pipeline);if(SUCCEEDED(r)){std::lock_guard guard(mutex);copies.erase(list);}return r;}
void STDMETHODCALLTYPE copy(ID3D12GraphicsCommandList* l,ID3D12Resource* dest,ID3D12Resource* src){remember(l,dest,src);originalCopy(l,dest,src);}
void STDMETHODCALLTYPE copyTexture(ID3D12GraphicsCommandList* l,const D3D12_TEXTURE_COPY_LOCATION* dest,UINT x,UINT y,UINT z,const D3D12_TEXTURE_COPY_LOCATION* src,const D3D12_BOX* box){if(dest&&src)remember(l,dest->pResource,src->pResource);originalCopyTexture(l,dest,x,y,z,src,box);}
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList*const* lists){ {std::lock_guard guard(mutex);for(UINT i=0;i<n;i++){auto it=copies.find(lists[i]);if(it!=copies.end()){for(auto& c:it->second){auto source=types.find(c.source);types[c.destination]=source==types.end()?Unknown:source->second;}copies.erase(it);}}}originalExecute(q,n,lists);}
template<class Call> HRESULT present(IDXGISwapChain* swap,UINT sync,UINT flags,Call original){
    physicalCalls++;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> chain;Microsoft::WRL::ComPtr<ID3D12Resource> buffer;Type type=Unknown;
    if(SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&chain)))&&SUCCEEDED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer)))){std::lock_guard guard(mutex);auto it=types.find(buffer.Get());if(it!=types.end())type=it->second;}
    if(type==Unknown&&!featureEnabled.load())type=Real;
    LARGE_INTEGER begin{},end{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);auto result=original();QueryPerformanceCounter(&end);UINT id=0;auto countResult=swap->GetLastPresentCount(&id);
    DXGI_FRAME_STATISTICS stats{};auto statsResult=swap->GetFrameStatistics(&stats);
    if(!(flags&DXGI_PRESENT_TEST)){delivery_telemetry::record(begin.QuadPart,end.QuadPart,reinterpret_cast<uintptr_t>(swap),unsigned(type));std::lock_guard guard(mutex);trace<<begin.QuadPart<<','<<end.QuadPart<<','<<frequency.QuadPart<<','<<GetCurrentThreadId()<<','<<reinterpret_cast<uintptr_t>(swap)<<','<<id<<','<<(type==Real?"REAL":type==Generated?"GENERATED":"UNKNOWN")<<",0,"<<sync<<','<<flags<<','<<unsigned(result)<<','<<unsigned(countResult)<<','<<unsigned(statsResult)<<','<<stats.PresentCount<<','<<stats.PresentRefreshCount<<','<<stats.SyncRefreshCount<<','<<stats.SyncQPCTime.QuadPart<<'\n';if(id%120==0)trace.flush();}
    return result;
}
HRESULT STDMETHODCALLTYPE physicalPresent(IDXGISwapChain* s,UINT sync,UINT flags){return present(s,sync,flags,[&]{return originalPresent(s,sync,flags);});}
HRESULT STDMETHODCALLTYPE physicalPresent1(IDXGISwapChain1* s,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p){return present(s,sync,flags,[&]{return originalPresent1(s,sync,flags,p);});}
void swapchain(IUnknown* s){Microsoft::WRL::ComPtr<IDXGISwapChain3> chain;Microsoft::WRL::ComPtr<IUnknown> native;constexpr GUID unwrap={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    if(!s)return;if(SUCCEEDED(s->QueryInterface(unwrap,reinterpret_cast<void**>(native.GetAddressOf()))))native.As(&chain);else s->QueryInterface(IID_PPV_ARGS(&chain));if(!chain)return;auto table=*reinterpret_cast<void***>(chain.Get());
#ifdef FG_MANAGED_TEST
    DXGI_SWAP_CHAIN_DESC1 desc{};chain->GetDesc1(&desc);HMODULE owner=nullptr;wchar_t ownerPath[32768]{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(table[8]),&owner);GetModuleFileNameW(owner,ownerPath,32768);
    fprintf(stderr,"Delivery hook chain %p owner %ls effect %u flags %u buffers %u\n",chain.Get(),ownerPath,unsigned(desc.SwapEffect),desc.Flags,desc.BufferCount);
#endif
    if(!originalPresent&&MH_CreateHook(table[8],&physicalPresent,reinterpret_cast<void**>(&originalPresent))==MH_OK)MH_EnableHook(table[8]);
    if(!originalPresent1&&MH_CreateHook(table[22],&physicalPresent1,reinterpret_cast<void**>(&originalPresent1))==MH_OK)MH_EnableHook(table[22]);
}
void device(ID3D12Device* d,ID3D12CommandQueue* q){if(originalCopy&&originalExecute)return;Microsoft::WRL::ComPtr<ID3D12CommandAllocator> a;Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> l;if(FAILED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)))||FAILED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l))))return;
    auto table=*reinterpret_cast<void***>(l.Get());if(!originalReset&&MH_CreateHook(table[10],&reset,reinterpret_cast<void**>(&originalReset))==MH_OK)MH_EnableHook(table[10]);if(!originalCopy&&MH_CreateHook(table[17],&copy,reinterpret_cast<void**>(&originalCopy))==MH_OK)MH_EnableHook(table[17]);if(!originalCopyTexture&&MH_CreateHook(table[16],&copyTexture,reinterpret_cast<void**>(&originalCopyTexture))==MH_OK)MH_EnableHook(table[16]);l->Close();
    table=*reinterpret_cast<void***>(q);if(!originalExecute&&MH_CreateHook(table[10],&execute,reinterpret_cast<void**>(&originalExecute))==MH_OK)MH_EnableHook(table[10]);
}
void evaluate(const NVSDK_NGX_Parameter* p){if(!p)return;std::lock_guard guard(mutex);ID3D12Resource* r=nullptr;
    for(auto item:{std::pair{NVSDK_NGX_DLSSG_Parameter_OutputInterpolated,Generated},std::pair{NVSDK_NGX_DLSSG_Parameter_OutputReal,Real},std::pair{NVSDK_NGX_DLSSG_Parameter_Backbuffer,Real}}){r=nullptr;if(NVSDK_NGX_SUCCEED(p->Get(item.first,&r))&&r)types[r]=item.second;}observedEvaluations++;
}
}
