#include "probe_gpu.h"
static void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
int wmain(int argc,wchar_t** argv) {
    std::filesystem::path dir=argc>1?argv[1]:L".";
    Backend rr; Gpu g;
    try {
        std::filesystem::create_directories(dir);
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory; check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++) {
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
            if(desc.VendorId==0x10de && SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device)))) break;
            adapter.Reset();
        }
        if(!g.device) throw std::runtime_error("No NVIDIA D3D12 adapter");
        D3D12_COMMAND_QUEUE_DESC q{}; check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");
        check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");
        check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");
        check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
        wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);
        if(argc>2 && !wcscmp(argv[2],L"--discover")) {
            auto discovery=LoadLibraryW((std::filesystem::path(exe).parent_path()/L"bedrock_rr_discovery.dll").c_str());
            auto start=discovery?reinterpret_cast<bool(*)()>(GetProcAddress(discovery,"NgxDiscoveryStart")):nullptr;
            if(!start||!start())throw std::runtime_error("NGX discovery test failed to initialize");
        }
        if(!rr.initialize(g.device.Get(),std::filesystem::path(exe).parent_path(),dir/L"ngx-logs")) throw std::runtime_error(rr.error());
        if(argc>2 && !wcscmp(argv[2],L"--observe")) {
            auto observer=LoadLibraryW((std::filesystem::path(exe).parent_path()/L"bedrock_rr_ngx_observer.dll").c_str());
            auto start=observer?reinterpret_cast<bool(*)()>(GetProcAddress(observer,"NgxObserverStart")):nullptr;
            if(!start||!start())throw std::runtime_error("NGX observer test failed to initialize");
        }
        if(argc>2 && !wcscmp(argv[2],L"--activate")) {
            auto activation=LoadLibraryW((std::filesystem::path(exe).parent_path()/L"bedrock_rr_activation.dll").c_str());
            auto start=activation?reinterpret_cast<bool(*)()>(GetProcAddress(activation,"NgxActivationStart")):nullptr;
            if(!start||!start())throw std::runtime_error("Activation probe test failed to initialize");
        }
        bool dlaa=argc>2&&!wcscmp(argv[2],L"--dlaa");
        unsigned iw=dlaa?960:640,ih=dlaa?540:360,ow=960,oh=540;
        if(argc>6&&!wcscmp(argv[2],L"--size")){iw=std::stoul(argv[3]);ih=std::stoul(argv[4]);ow=std::stoul(argv[5]);oh=std::stoul(argv[6]);}
        auto quality=iw*1.f/ow<.43f?NVSDK_NGX_PerfQuality_Value_UltraPerformance:NVSDK_NGX_PerfQuality_Value_MaxQuality;
        if(!rr.create(g.list.Get(),iw,ih,ow,oh,false,false,dlaa?NVSDK_NGX_PerfQuality_Value_DLAA:quality)) throw std::runtime_error(rr.error());
        auto color=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto out=texture(g.device.Get(),ow,oh,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        auto depth=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32_FLOAT);
        auto motion=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32G32_FLOAT);
        auto diffuse=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto specular=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto normal=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto hit=texture(g.device.Get(),iw,ih,DXGI_FORMAT_R32_FLOAT);
        std::vector<float> colors(iw*ih*4),albedo(colors.size()),reflectance(colors.size()),normals(colors.size());
        std::vector<float> depths(iw*ih,2.f),hits(iw*ih,2.f),mvs(iw*ih*2,0.f);
        std::uint32_t rng=137;
        // Synthetic input fixture: a stationary front-facing plane with a noisy
        // light gradient. It tests the RR API, not Minecraft image quality.
        for(unsigned y=0;y<ih;y++) for(unsigned x=0;x<iw;x++) {
            size_t p=(size_t(y)*iw+x)*4; rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            float noise=float(rng&65535)/65535.f*.9f+.1f;
            float light=.3f+float(x)/iw*1.2f;
            float a=((x/80+y/80)%2)? .65f : .2f;
            for(unsigned c=0;c<3;c++) {colors[p+c]=a*light*noise;albedo[p+c]=a;reflectance[p+c]=.04f;}
            colors[p+3]=albedo[p+3]=reflectance[p+3]=1.f; normals[p+2]=-1.f;normals[p+3]=.7f;
        }
        std::vector<ComPtr<ID3D12Resource>> uploads;
        for(auto item : {std::pair{color.Get(),&colors}, {diffuse.Get(),&albedo},{specular.Get(),&reflectance},{normal.Get(),&normals}})
            uploads.push_back(upload(g,item.first,*item.second,4));
        uploads.push_back(upload(g,depth.Get(),depths,1));uploads.push_back(upload(g,hit.Get(),hits,1));uploads.push_back(upload(g,motion.Get(),mvs,2));
        g.submit();uploads.clear();
        Frame f;
        f.noisyColor={color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.output={out.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        f.depth={depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.motion={motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.diffuseAlbedo={diffuse.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.specularAlbedo={specular.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.normalRoughness={normal.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        f.specularHitDistance={hit.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        for(unsigned i=0;i<4;i++) f.worldToView[i*5]=1;
        f.viewToClip={1.f,0,0,0,0,1.7777778f,0,0,0,0,1.0001f,1.f,0,0,-.10001f,0};
        Frame bad=f;bad.normalRoughness.resource=nullptr;
        if(rr.evaluate(g.list.Get(),bad)) throw std::runtime_error("Missing-input rejection failed");
        for(unsigned i=0;i<4;i++) {f.reset=i==0; if(!rr.evaluate(g.list.Get(),f)) throw std::runtime_error(rr.error());g.submit();}
        // Stand in for Bedrock's final SR destination: RR writes the complete
        // display image and the bridge copies it without another SR evaluation.
        auto finalOutput=texture(g.device.Get(),ow,oh,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        transition(g.list.Get(),out.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        transition(g.list.Get(),finalOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION finalDst{},finalSrc{};finalDst.pResource=finalOutput.Get();finalSrc.pResource=out.Get();finalDst.Type=finalSrc.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX finalRect{0,0,0,ow,oh,1};g.list->CopyTextureRegion(&finalDst,0,0,0,&finalSrc,&finalRect);
        transition(g.list.Get(),finalOutput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        transition(g.list.Get(),out.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        g.submit(); // Keep both copy resources alive until the GPU fence completes.
        out=finalOutput;
        auto desc=out->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;
        g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);
        auto readback=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={out.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;
        from.pResource=out.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);g.submit();
        void* mapped=nullptr;D3D12_RANGE range{0,SIZE_T(bytes)};check(readback->Map(0,&range,&mapped),"Readback map");
        auto pixels=reinterpret_cast<const float*>(static_cast<char*>(mapped)+fp.Offset);
        double sum=0;unsigned invalid=0;
        for(unsigned y=0;y<oh;y++) {auto row=reinterpret_cast<const float*>(reinterpret_cast<const char*>(pixels)+size_t(y)*fp.Footprint.RowPitch);
            for(unsigned x=0;x<ow;x++)for(unsigned c=0;c<3;c++){float v=row[x*4+c];if(!std::isfinite(v))++invalid;else sum+=std::abs(v);}}
        if(invalid||sum<=0) throw std::runtime_error("RR output invalid or empty");
        bmp(dir/L"rr-api-test.bmp",pixels,ow,oh,fp.Footprint.RowPitch);readback->Unmap(0,nullptr);
        unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;
        if(SUCCEEDED(g.device.As(&iq))) for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++) {
            SIZE_T len=0;iq->GetMessage(i,nullptr,&len);std::vector<unsigned char> msg(len);
            auto m=reinterpret_cast<D3D12_MESSAGE*>(msg.data());iq->GetMessage(i,m,&len);
            if(m->Severity==D3D12_MESSAGE_SEVERITY_ERROR||m->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION){++errors;std::cerr<<m->pDescription<<"\n";}
        }
        if(errors) throw std::runtime_error("D3D12 validation errors");
        std::ofstream report(dir/L"rr-probe.json");
        report<<"{\"status\":\"passed\",\"requested_rr_preset\":\"F\",\"evaluations\":"<<rr.evaluationCount()
            <<",\"input\":["<<iw<<","<<ih<<"],\"output\":["<<ow<<","<<oh<<"],\"dlaa\":"<<(dlaa?"true":"false")<<",\"native_sr_evaluations\":0,\"final_output_copy\":true,\"nonfinite_rgb\":"<<invalid<<",\"mean_abs_rgb\":"<<sum/(ow*oh*3)
            <<",\"debug_layer\":"<<(debug?"true":"false")<<",\"debug_errors\":"<<errors<<",\"minecraft_integration_verified\":false}\n";
        rr.shutdownAfterGpuIdle();std::cout<<"RR Preset F API test passed: 4 evaluations, finite nonzero output.\n";return 0;
    } catch(const std::exception& e) {
        // GPU work was fenced after every successful submission. A failed
        // recording is never submitted, so there is no pending RR work here.
        rr.shutdownAfterGpuIdle();
        std::cerr<<e.what()<<"\n";
        std::ofstream(dir/L"rr-probe-error.txt")<<e.what()<<"\n";
        return 1;
    }
}
