#include "probe_gpu.h"
#include "rr_input_crop.h"
#include <cstring>
int wmain(int argc,wchar_t** argv){try{
    auto dir=std::filesystem::path(argc>1?argv[1]:L"crop-test");std::filesystem::create_directories(dir);
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug");debug->EnableDebugLayer();
    Gpu g;ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device)),"Device");
    D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
    DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R16_FLOAT};
    unsigned bpp[]={8,8,8,8,4,4,2};unsigned cases=0;
    for(auto sourceState:{D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE})for(auto positionFormat:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT})for(auto hitFormat:{DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R32_FLOAT})for(auto dimensions:{std::pair{1707u,960u},std::pair{127u,65u},std::pair{257u,193u}}){auto [w,h]=dimensions;
        formats[3]=positionFormat;bpp[3]=positionFormat==DXGI_FORMAT_R32G32B32A32_FLOAT?16:8;
        formats[6]=hitFormat;bpp[6]=hitFormat==DXGI_FORMAT_R32_FLOAT?4:2;
        std::array<ComPtr<ID3D12Resource>,7> inputs;std::vector<ComPtr<ID3D12Resource>> uploads;
        for(unsigned i=0;i<7;i++){
            inputs[i]=texture(g.device.Get(),w,h,formats[i],true);auto desc=inputs[i]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
            auto up=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* data=nullptr;check(up->Map(0,nullptr,&data),"Upload map");
            for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<bpp[i];c++)static_cast<unsigned char*>(data)[fp.Offset+size_t(y)*fp.Footprint.RowPitch+size_t(x)*bpp[i]+c]=static_cast<unsigned char>((x*13+y*29+c*7+i*11)&255);up->Unmap(0,nullptr);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={inputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};g.list->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=inputs[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.pResource=up.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);g.list->ResourceBarrier(1,&barrier);uploads.push_back(up);
        }
        std::array<D3D12_RESOURCE_STATES,7> sourceStates{};sourceStates.fill(sourceState);
        if(sourceState!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS)for(auto& input:inputs){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={input.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,sourceState};g.list->ResourceBarrier(1,&b);}
        std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,7> footprints{};unsigned x=0,y=0;bool full=w==257;auto bytes=planInputCrop(g.device.Get(),inputs,w,h,12345,x,y,footprints,full);
        if(full&&(x||y||footprints[0].Footprint.Width!=w||footprints[0].Footprint.Height!=h))throw std::runtime_error("Full-frame extent mismatch");
        if(bytes>4*1024*1024)throw std::runtime_error("Capture allocation unexpectedly large");auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);recordInputCrop(g.list.Get(),read.Get(),inputs,x,y,footprints,&sourceStates);g.submit();void* data=nullptr;check(read->Map(0,nullptr,&data),"Readback map");
        for(unsigned i=0;i<7;i++)for(unsigned row=0;row<footprints[i].Footprint.Height;row++)for(unsigned col=0;col<footprints[i].Footprint.Width;col++)for(unsigned c=0;c<bpp[i];c++){
            auto actual=static_cast<unsigned char*>(data)[footprints[i].Offset+size_t(row)*footprints[i].Footprint.RowPitch+size_t(col)*bpp[i]+c];auto expected=static_cast<unsigned char>(((col+x)*13+(row+y)*29+c*7+i*11)&255);if(actual!=expected)throw std::runtime_error("Cropped input differs from source bytes");
        }read->Unmap(0,nullptr);++cases;
    }
    unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;check(g.device.As(&iq),"InfoQueue");for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<unsigned char> storage(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());iq->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}if(errors)throw std::runtime_error("Crop validation errors");
    std::ofstream(dir/L"input-crop-test.json")<<"{\"status\":\"passed\",\"dimensions_cases\":"<<cases<<",\"formats_per_case\":7,\"bit_exact\":true,\"debug_errors\":0}";std::cout<<"RR input crop passed: seven formats, odd/small dimensions, bit exact, zero D3D12 errors.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
