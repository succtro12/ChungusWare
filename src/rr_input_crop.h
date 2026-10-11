#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
namespace bedrock_rr {
// Diagnostic only: bounded readback of single-sample textures. Caller supplies
// proven recording states for native resources; every transition is restored.
template<size_t N>
inline UINT64 planInputCrop(ID3D12Device* device,
    const std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,N>& inputs,
    unsigned width,unsigned height,UINT64 start,unsigned& x,unsigned& y,
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,N>& footprints,bool full=false) {
    unsigned cw=full?width:std::min(width,128u),ch=full?height:std::min(height,128u);
    x=width/2-cw/2;y=height/2-ch/2;
    for(unsigned i=0;i<N;i++){
        auto desc=inputs[i]->GetDesc();desc.Width=cw;desc.Height=ch;
        device->GetCopyableFootprints(&desc,0,1,(start+511)&~UINT64(511),&footprints[i],nullptr,nullptr,nullptr);
        start=footprints[i].Offset+UINT64(footprints[i].Footprint.RowPitch)*ch;
    }return start;
}
template<size_t N>
inline void recordInputCrop(ID3D12GraphicsCommandList* list,ID3D12Resource* readback,
    const std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,N>& inputs,
    unsigned x,unsigned y,const std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,N>& footprints,
    const std::array<D3D12_RESOURCE_STATES,N>* states=nullptr){
    D3D12_BOX box{x,y,0,x+footprints[0].Footprint.Width,y+footprints[0].Footprint.Height,1};
    for(unsigned i=0;i<N;i++){
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={inputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,states?(*states)[i]:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=inputs[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprints[i];
        list->CopyTextureRegion(&to,0,0,0,&from,&box);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    }
}
}
