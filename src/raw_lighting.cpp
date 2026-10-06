#include "raw_lighting.h"
using Microsoft::WRL::ComPtr;
namespace bedrock_rr {
bool RawLightingSnapshot::initialize(ID3D12Device* device,unsigned width,unsigned height) {
    if(device_){error_="Fence and create a new raw snapshot before changing render size";return false;}
    if(!device||!width||!height||width>16384||height>16384){error_="Invalid raw snapshot allocation";return false;}
    std::array<ComPtr<ID3D12Resource>,Count> textures;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=width;desc.Height=height;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    for(unsigned i=0;i<Count;i++){
        desc.Format=i==SpecularDistance?DXGI_FORMAT_R16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&textures[i])))){
            error_="Raw lighting snapshot texture allocation failed";return false;
        }
    }
    device_=device;textures_=std::move(textures);return true;
}
bool RawLightingSnapshot::record(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,Count>& sources) {
    std::array<D3D12_RESOURCE_STATES,Count> states;states.fill(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);return record(list,sources,states);
}
bool RawLightingSnapshot::record(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,Count>& sources,const std::array<D3D12_RESOURCE_STATES,Count>& sourceStates) {
    if(!device_||!list){error_="Raw snapshot is not initialized";return false;}
    ComPtr<ID3D12Device> listDevice;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&listDevice)))||listDevice.Get()!=device_.Get()){error_="Raw snapshot command list uses a different device";return false;}
    // Require the exact native texture contract before recording any commands.
    // The bridge must establish UAV state from the verified producer; this
    // helper does not guess state from a descriptor or an old capture log.
    for(unsigned i=0;i<Count;i++){
        if(!sources[i]){error_="Missing raw lighting channel";return false;}
        if(sourceStates[i]!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS&&
           !(i==SpecularDistance&&(sourceStates[i]==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE||sourceStates[i]==(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)))){error_="Unsupported observed raw source state";return false;}
        auto a=sources[i]->GetDesc(),b=textures_[i]->GetDesc();ComPtr<ID3D12Device> sourceDevice;
        if(FAILED(sources[i]->GetDevice(IID_PPV_ARGS(&sourceDevice)))||sourceDevice.Get()!=device_.Get()||
           a.Dimension!=b.Dimension||a.Width!=b.Width||a.Height!=b.Height||a.Format!=b.Format||
           a.DepthOrArraySize!=1||a.MipLevels!=1||a.SampleDesc.Count!=1||!(a.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)){
            error_="Raw lighting source does not match the native allocation contract";return false;
        }
        for(unsigned j=0;j<Count;j++)if(sources[i]==textures_[j].Get()||(j<i&&sources[i]==sources[j])){error_="Raw snapshot resources must not alias";return false;}
    }
    std::array<D3D12_RESOURCE_BARRIER,Count*2> barriers{};
    for(unsigned i=0;i<Count;i++){
        auto& src=barriers[i*2];src.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        src.Transition={sources[i],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,sourceStates[i],D3D12_RESOURCE_STATE_COPY_SOURCE};
        auto& dst=barriers[i*2+1];dst.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dst.Transition={textures_[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};
    }
    list->ResourceBarrier(UINT(barriers.size()),barriers.data());
    for(unsigned i=0;i<Count;i++)list->CopyResource(textures_[i].Get(),sources[i]);
    for(auto& b:barriers)std::swap(b.Transition.StateBefore,b.Transition.StateAfter);
    list->ResourceBarrier(UINT(barriers.size()),barriers.data());return true;
}
}
