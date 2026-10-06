#include "probe_gpu.h"
#include "guide_conversion.h"
#include "raw_lighting.h"
#include <array>

static ComPtr<ID3D12Resource> uploadBytes(Gpu& g,ID3D12Resource* dst,const void* data,unsigned bytesPerPixel) {
    auto desc=dst->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
    g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
    auto src=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr;D3D12_RANGE noRead{0,0};check(src->Map(0,&noRead,&mapped),"Guide upload map");
    for(unsigned y=0;y<desc.Height;y++)memcpy(static_cast<char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,
        static_cast<const char*>(data)+size_t(y)*size_t(desc.Width)*bytesPerPixel,size_t(desc.Width)*bytesPerPixel);
    src->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=dst;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=src.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;
    g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    g.list->ResourceBarrier(1,&b);return src;
}
int wmain(int argc,wchar_t** argv) {
    std::filesystem::path dir=argc>1?argv[1]:L".";Gpu g;
    try {
        std::filesystem::create_directories(dir);
        ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Guide debug layer");debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Guide factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++) {
            DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);
            if(d.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device))))break;
            adapter.Reset();
        }
        if(!g.device)throw std::runtime_error("Guide test needs NVIDIA GPU");
        D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Guide queue");
        check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Guide allocator");
        check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"Guide list");
        check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Guide fence");
        GuideConversion conversion;if(!conversion.initialize(g.device.Get()))throw std::runtime_error(conversion.error());
        // Allocations deliberately exceed the 5x3 active subrect. Native input
        // formats, folded negative-Z normals, dielectric/metal material cases.
        constexpr unsigned aw=7,ah=5,w=5,h=3;
        auto normals=texture(g.device.Get(),aw,ah,DXGI_FORMAT_R16G16_SNORM);
        auto material=texture(g.device.Get(),aw,ah,DXGI_FORMAT_R8G8B8A8_UNORM);
        auto rough=texture(g.device.Get(),aw,ah,DXGI_FORMAT_R16G16B16A16_FLOAT);
        auto direction=texture(g.device.Get(),aw,ah,DXGI_FORMAT_R16G16B16A16_FLOAT);
        std::array<ComPtr<ID3D12Resource>,3> outputs;
        for(auto& r:outputs)r=texture(g.device.Get(),w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        std::vector<int16_t> ns(aw*ah*2);
        std::vector<uint8_t> ms(aw*ah*4);
        std::vector<uint16_t> rs(aw*ah*4);
        std::vector<uint16_t> vs(aw*ah*4);
        for(unsigned y=0;y<ah;y++)for(unsigned x=0;x<aw;x++) {
            unsigned p=y*aw+x;ns[p*2]=x%2?32767:0;ns[p*2+1]=x%2?32767:0;
            ms[p*4]=128;ms[p*4+1]=64;ms[p*4+2]=255;ms[p*4+3]=uint8_t((x%3)*127);
            rs[p*4+3]=0x3800; // exact binary16 0.5 linear roughness
            vs[p*4]=x%2?0x3c00:0;vs[p*4+2]=0xbc00; // (0,0,-1) or (1,0,-1)
        }
        std::vector<ComPtr<ID3D12Resource>> uploads;
        uploads.push_back(uploadBytes(g,normals.Get(),ns.data(),4));
        uploads.push_back(uploadBytes(g,material.Get(),ms.data(),4));
        uploads.push_back(uploadBytes(g,rough.Get(),rs.data(),8));
        uploads.push_back(uploadBytes(g,direction.Get(),vs.data(),8));
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=7;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> heap;check(g.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Guide descriptors");
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto stride=g.device->GetDescriptorHandleIncrementSize(hd.Type);
        for(auto* r:{normals.Get(),material.Get(),rough.Get(),direction.Get()}) {
            D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;
            g.device->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=stride;
        }
        for(auto& r:outputs){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            g.device->CreateUnorderedAccessView(r.Get(),nullptr,&v,cpu);cpu.ptr+=stride;}
        if(conversion.record(g.list.Get(),heap.Get(),heap->GetGPUDescriptorHandleForHeapStart(),0,h))throw std::runtime_error("Empty subrect accepted");
        if(!conversion.record(g.list.Get(),heap.Get(),heap->GetGPUDescriptorHandleForHeapStart(),w,h))throw std::runtime_error(conversion.error());
        g.submit();uploads.clear();
        float maxError=0;
        for(unsigned k=0;k<outputs.size();k++) {
            auto desc=outputs[k]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
            g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
            auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={outputs[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
            from.pResource=outputs[k].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);g.submit();
            void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(read->Map(0,&range,&mapped),"Guide readback");
            for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++) {
                auto row=reinterpret_cast<const float*>(static_cast<const char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);
                float metal=float(ms[(y*aw+x)*4+3])/255.f;
                float colour=c<3?float(ms[(y*aw+x)*4+c])/255.f:1.f;colour*=colour;
                float expected=1.f;
                if(k==0)expected=c<2?0.f:c==2?(x%2?-1.f:1.f):.5f;
                else if(c<3){
                    expected=colour*(1-metal);
                    if(k==2){
                        // CPU reference evaluates the original matrix-dot form,
                        // independently of the shader's expanded polynomials.
                        double v=x%2?1/std::sqrt(2.):1,a=.25;
                        double xv[]={1,v,v*v,v*v*v},yv[]={1,a,a*a,a*a*a};
                        const double m1[2][2]={{.99044,-1.28514},{1.29678,-.755907}};
                        const double m2[3][3]={{1,2.92338,59.4188},{20.3225,-27.0302,222.592},{121.563,626.13,316.627}};
                        const double m3[2][2]={{.0365463,3.32707},{9.0632,-9.04756}};
                        const double m4[3][3]={{1,3.59685,-1.36772},{9.04401,-16.3174,9.22949},{5.56589,19.7886,-20.2123}};
                        double bn=0,bd=0,sn=0,sd=0;
                        for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++){bn+=yv[i]*m1[i][j]*xv[j];sn+=yv[i]*m3[i][j]*xv[j];}
                        unsigned biasIndices[]={0,1,3},scaleIndices[]={0,2,3};
                        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++){
                            bd+=yv[biasIndices[i]]*m2[i][j]*xv[biasIndices[j]];
                            sd+=yv[biasIndices[i]]*m4[i][j]*xv[scaleIndices[j]];
                        }
                        double f0=.04*(1-metal)+colour*metal;
                        // All fixture materials have green F0 >= 0.04, so the
                        // guide's bias suppression factor is exactly one.
                        expected=float(f0*std::max(sn/sd,0.)+std::max(bn/bd,0.));
                    }
                }
                float actual=row[x*4+c];if(!std::isfinite(actual))throw std::runtime_error("Nonfinite guide output");
                maxError=std::max(maxError,std::abs(actual-expected));
            }
            read->Unmap(0,nullptr);
        }
        if(maxError>1e-5f)throw std::runtime_error("Guide GPU output disagrees with native packing formulas");
        RawLightingSnapshot raw;if(!raw.initialize(g.device.Get(),aw,ah))throw std::runtime_error(raw.error());
        std::array<ComPtr<ID3D12Resource>,RawLightingSnapshot::Count> rawSources;
        std::array<ID3D12Resource*,RawLightingSnapshot::Count> sourcePointers{};
        std::array<std::vector<uint16_t>,RawLightingSnapshot::Count> rawPixels;
        for(unsigned k=0;k<RawLightingSnapshot::Count;k++){
            unsigned channels=k==RawLightingSnapshot::SpecularDistance?1:4;
            rawSources[k]=texture(g.device.Get(),aw,ah,channels==1?DXGI_FORMAT_R16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT,true);
            auto& pixels=rawPixels[k];pixels.resize(aw*ah*channels);
            for(unsigned i=0;i<pixels.size();i++)pixels[i]=uint16_t(i%3==0?0x3c00:i%3==1?0x4000:channels==4?0xbc00:0x3800);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={rawSources[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};g.list->ResourceBarrier(1,&b);
            uploads.push_back(uploadBytes(g,rawSources[k].Get(),pixels.data(),channels*2));
            b.Transition={rawSources[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};g.list->ResourceBarrier(1,&b);
            sourcePointers[k]=rawSources[k].Get();
        }
        auto badSources=sourcePointers;badSources[0]=nullptr;
        if(raw.record(g.list.Get(),badSources))throw std::runtime_error("Incomplete raw snapshot accepted");
        if(!raw.record(g.list.Get(),sourcePointers))throw std::runtime_error(raw.error());
        std::array<D3D12_RESOURCE_STATES,RawLightingSnapshot::Count> observedStates;observedStates.fill(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);observedStates[RawLightingSnapshot::SpecularDistance]=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER distanceRead{};distanceRead.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;distanceRead.Transition={sourcePointers[RawLightingSnapshot::SpecularDistance],D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,observedStates[RawLightingSnapshot::SpecularDistance]};g.list->ResourceBarrier(1,&distanceRead);
        if(!raw.record(g.list.Get(),sourcePointers,observedStates))throw std::runtime_error(raw.error());
        std::swap(distanceRead.Transition.StateBefore,distanceRead.Transition.StateAfter);g.list->ResourceBarrier(1,&distanceRead);
        g.submit();uploads.clear();
        // Model native denoisers overwriting their original output buffers.
        // The independent snapshot must retain all original half-float bits.
        for(unsigned k=0;k<RawLightingSnapshot::Count;k++){
            unsigned channels=k==RawLightingSnapshot::SpecularDistance?1:4;
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={rawSources[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};g.list->ResourceBarrier(1,&b);
            std::vector<uint16_t> zeros(aw*ah*channels,0);
            uploads.push_back(uploadBytes(g,rawSources[k].Get(),zeros.data(),channels*2));
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;g.list->ResourceBarrier(1,&b);
        }
        g.submit();uploads.clear();
        for(unsigned k=0;k<RawLightingSnapshot::Count;k++){
            auto* captured=raw.resource(static_cast<RawLightingSnapshot::Channel>(k));auto desc=captured->GetDesc();
            unsigned channels=k==RawLightingSnapshot::SpecularDistance?1:4;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
            auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={captured,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
            from.pResource=captured;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);g.list->ResourceBarrier(1,&b);g.submit();
            void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(read->Map(0,&range,&mapped),"Raw snapshot readback");
            for(unsigned y=0;y<ah;y++){
                auto row=static_cast<const char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch;
                if(memcmp(row,rawPixels[k].data()+size_t(y)*aw*channels,aw*channels*2))throw std::runtime_error("Raw snapshot was modified after source overwrite");
            }
            read->Unmap(0,nullptr);
        }
        ComPtr<ID3D12InfoQueue> iq;check(g.device.As(&iq),"Guide validation queue");unsigned errors=0;
        for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++) {
            SIZE_T size=0;iq->GetMessage(i,nullptr,&size);std::vector<char> storage(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());iq->GetMessage(i,m,&size);
            if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<m->pDescription<<'\n';}
        }
        if(errors)throw std::runtime_error("Guide D3D12 validation errors");
        std::ofstream(dir/L"guide-test.json")<<"{\"status\":\"passed\",\"input_allocation\":[7,5],\"active_subrect\":[5,3],\"max_error\":"<<maxError
            <<",\"debug_errors\":0,\"native_formats\":true,\"view_dependent_specular\":true,\"raw_channels_preserved_bit_exact\":4,\"minecraft_integration_verified\":false}\n";
        std::cout<<"Native guide conversion GPU test passed.\n";return 0;
    }catch(const std::exception& e){std::ofstream(dir/L"guide-test-error.txt")<<e.what();std::cerr<<e.what()<<'\n';return 1;}
}
