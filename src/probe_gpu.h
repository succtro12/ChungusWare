#pragma once
#include "rr_backend.h"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdint>
using Microsoft::WRL::ComPtr;
using namespace bedrock_rr;
static void check(HRESULT h,const char* what) { if(FAILED(h)) { std::ostringstream s; s<<what<<": HRESULT 0x"<<std::hex<<unsigned(h); throw std::runtime_error(s.str()); } }
struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = CreateEventW(nullptr,FALSE,FALSE,nullptr);
    UINT64 tick=0;
    ~Gpu() { if(event) CloseHandle(event); }
    void submit() {
        check(list->Close(),"Close"); ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.Get(),++tick),"Signal");
        check(fence->SetEventOnCompletion(tick,event),"Fence event");
        if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU fence timed out");
        check(device->GetDeviceRemovedReason(),"Device removed");
        check(allocator->Reset(),"Allocator reset"); check(list->Reset(allocator.Get(),nullptr),"List reset");
    }
};
static ComPtr<ID3D12Resource> buffer(ID3D12Device* d,UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type=heap;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=bytes;
    desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r; check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)),"Buffer"); return r;
}
static ComPtr<ID3D12Resource> texture(ID3D12Device* d,unsigned w,unsigned h,DXGI_FORMAT fmt,bool output=false) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=w; desc.Height=h;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Format=fmt;
    desc.Flags=output?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> r; check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,
        output?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)),"Texture"); return r;
}
static ComPtr<ID3D12Resource> upload(Gpu& g,ID3D12Resource* dst,const std::vector<float>& pixels,unsigned channels) {
    auto desc=dst->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 bytes=0;
    g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto src=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr; D3D12_RANGE noRead{0,0}; check(src->Map(0,&noRead,&mapped),"Upload map");
    for(unsigned y=0;y<desc.Height;y++) memcpy(static_cast<char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
        pixels.data()+size_t(y)*size_t(desc.Width)*channels,size_t(desc.Width)*channels*sizeof(float));
    src->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION from{},to{}; from.pResource=src.Get(); from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint=fp;
    to.pResource=dst; to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    g.list->ResourceBarrier(1,&b); return src;
}
static void bmp(const std::filesystem::path& path,const float* data,unsigned w,unsigned h,UINT stride) {
    unsigned row=(w*3+3)&~3u, size=54+row*h;
    std::vector<unsigned char> bytes(size,0);
    auto put=[&](unsigned offset,unsigned value,unsigned n) { for(unsigned i=0;i<n;i++) bytes[offset+i]=static_cast<unsigned char>(value>>(8*i)); };
    bytes[0]='B';bytes[1]='M';put(2,size,4);put(10,54,4);put(14,40,4);put(18,w,4);put(22,h,4);put(26,1,2);put(28,24,2);
    for(unsigned y=0;y<h;y++) { auto p=reinterpret_cast<const float*>(reinterpret_cast<const char*>(data)+size_t(y)*stride);
        for(unsigned x=0;x<w;x++) for(unsigned c=0;c<3;c++) {
            float v=std::max(0.f,p[x*4+(2-c)]); v=v/(1+v); v=std::pow(v,1.f/2.2f);
            bytes[54+size_t(h-1-y)*row+x*3+c]=static_cast<unsigned char>(std::clamp(v*255.f,0.f,255.f));
        }
    }
    std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
}
