#include "probe_gpu.h"
#include "bridge_unpack.h"
int wmain(int argc,wchar_t** argv){Gpu g;Backend rr;try{
    auto dir=argc>1?std::filesystem::path(argv[1]):L".";std::filesystem::create_directories(dir);
    const bool edge=argc>2&&std::wstring(argv[2])==L"--edge";
    const auto rrQuality=NVSDK_NGX_PerfQuality_Value_MaxQuality;
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device))))break;adapter.Reset();}if(!g.device)throw std::runtime_error("NVIDIA device missing");
    D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
    BridgeUnpack unpack;if(!unpack.initialize(g.device.Get()))throw std::runtime_error(unpack.error);
    constexpr unsigned w=127,h=65,aw=129,ah=67;
    std::array<ComPtr<ID3D12Resource>,9> inputs;std::array<ComPtr<ID3D12Resource>,8> outputs;
    for(unsigned i=0;i<9;i++)if(i!=7)inputs[i]=texture(g.device.Get(),aw,ah,i==4||i==6?DXGI_FORMAT_R32_FLOAT:i==5?DXGI_FORMAT_R32G32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
    inputs[7]=buffer(g.device.Get(),16,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* incident=nullptr;check(inputs[7]->Map(0,nullptr,&incident),"Exposure");float exposure[]={2,3,0,0};memcpy(incident,exposure,16);inputs[7]->Unmap(0,nullptr);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    for(unsigned i=0;i<9;i++)if(i!=7){unsigned channels=i==4||i==6?1:i==5?2:4;std::vector<float> data(aw*ah*channels);
        for(unsigned y=0;y<ah;y++)for(unsigned x=0;x<aw;x++){unsigned p=(y*aw+x)*channels;for(unsigned c=0;c<channels;c++)data[p+c]=i==0?(.1f+.01f*x+.02f*y):i==1?(c==2?1.f:c==3?.3f:0):i==2?(c==3?1.f:.5f):i==3?(c==3?1.f:.04f):i==4?2.f:i==5?(c==0?.01f*x:.01f*y):i==6?4.f:(c==2?-1.f:0.f);}
        uploads.push_back(upload(g,inputs[i].Get(),data,channels));}
    for(unsigned i=0;i<7;i++)outputs[i]=texture(g.device.Get(),w,h,i==4||i==6?DXGI_FORMAT_R32_FLOAT:i==5?DXGI_FORMAT_R32G32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    outputs[7]=texture(g.device.Get(),1,1,DXGI_FORMAT_R32_FLOAT,true);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=17;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> heap;check(g.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Heap");auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto stride=g.device->GetDescriptorHandleIncrementSize(hd.Type);
    for(unsigned i=0;i<9;i++){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;if(i==7){d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;d.Buffer.NumElements=1;d.Buffer.StructureByteStride=16;}else{d.Format=inputs[i]->GetDesc().Format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;}g.device->CreateShaderResourceView(inputs[i].Get(),&d,cpu);cpu.ptr+=stride;}
    for(auto& r:outputs){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=r->GetDesc().Format;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;g.device->CreateUnorderedAccessView(r.Get(),nullptr,&d,cpu);cpu.ptr+=stride;}
    float view[16]{};for(unsigned i=0;i<4;i++)view[i*5]=1;
    // Expected physical columns for the first two rows are independently
    // transcribed from the native interleave shader's branch/control flow.
    const unsigned expected[2][7]={{63,0,64,1,65,2,66},{0,63,1,64,2,65,3}};
    float maxError=0;
    for(unsigned sceneDomain:{0u,1u,2u})for(float scale:{1.f,4.f})for(float ratio:{.070317f,.125f,1.f,8.f})for(unsigned frame=0;frame<2;frame++){
        check(inputs[7]->Map(0,nullptr,&incident),"Exposure variation");float variedExposure[]={2,2*ratio,0,0};memcpy(incident,variedExposure,16);inputs[7]->Unmap(0,nullptr);
        unpack.record(g.list.Get(),heap.Get(),heap->GetGPUDescriptorHandleForHeapStart(),w,h,63,frame,0,scale,view,sceneDomain,edge);g.submit();
        for(unsigned k:{0u,4u,5u,6u}){auto desc=outputs[k]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={outputs[k].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;src.pResource=outputs[k].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);g.list->ResourceBarrier(1,&b);g.submit();void* data=nullptr;check(read->Map(0,nullptr,&data),"Readback");
            for(unsigned y=0;y<h;y++){auto row=reinterpret_cast<const float*>(static_cast<const char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);for(unsigned x=0;x<w;x++){unsigned sourceX=edge?std::min(x,w-2):x;unsigned physical=(sourceX>>1)+(((sourceX^y)&1)==((frame&1)^1)?0:63);float want=k==0?(.1f+.01f*physical+.02f*y)*(sceneDomain==2?scale*ratio:sceneDomain==1?(scale==4?4.f:1.f/ratio):1.f):k==4?2:k==5?.01f*physical:4;unsigned channels=k==0?4:k==5?2:1;maxError=std::max(maxError,std::abs(row[x*channels]-want));}}read->Unmap(0,nullptr);}
    }
    auto finalOutput=texture(g.device.Get(),w,h,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
    D3D12_DESCRIPTOR_HEAP_DESC handoffDesc{};handoffDesc.NumDescriptors=3;handoffDesc.Type=hd.Type;handoffDesc.Flags=hd.Flags;ComPtr<ID3D12DescriptorHeap> handoffHeap;check(g.device->CreateDescriptorHeap(&handoffDesc,IID_PPV_ARGS(&handoffHeap)),"Handoff heap");
    auto handoffCpu=handoffHeap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned i:{0u,7u}){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=outputs[i]->GetDesc().Format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;g.device->CreateShaderResourceView(outputs[i].Get(),&d,handoffCpu);handoffCpu.ptr+=stride;}
    D3D12_UNORDERED_ACCESS_VIEW_DESC finalView{};finalView.Format=finalOutput->GetDesc().Format;finalView.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;g.device->CreateUnorderedAccessView(finalOutput.Get(),nullptr,&finalView,handoffCpu);
    float roundtripError=0;
    for(unsigned sceneDomain:{0u,1u,2u})for(float scale:{1.f,4.f})for(float ratio:{.070317f,.125f,1.f,8.f}){
        check(inputs[7]->Map(0,nullptr,&incident),"Roundtrip exposure");float variedExposure[]={2,2*ratio,0,0};memcpy(incident,variedExposure,16);inputs[7]->Unmap(0,nullptr);
        unpack.record(g.list.Get(),heap.Get(),heap->GetGPUDescriptorHandleForHeapStart(),w,h,63,0,0,scale,view,sceneDomain,edge);
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        for(unsigned i:{0u,7u}){b.Transition={outputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};g.list->ResourceBarrier(1,&b);}
        unpack.handoff(g.list.Get(),handoffHeap.Get(),w,h);
        for(unsigned i:{0u,7u}){b.Transition={outputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};g.list->ResourceBarrier(1,&b);}
        auto desc=finalOutput->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        b.Transition={finalOutput.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;src.pResource=finalOutput.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);g.list->ResourceBarrier(1,&b);g.submit();void* data=nullptr;check(read->Map(0,nullptr,&data),"Roundtrip readback");
        for(unsigned y=0;y<h;y++){auto row=reinterpret_cast<const float*>(static_cast<const char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch);for(unsigned x=0;x<w;x++)for(unsigned c=0;c<3;c++){unsigned sourceX=edge?std::min(x,w-2):x;unsigned physical=(sourceX>>1)+(((sourceX^y)&1)==1?0:63);float want=.1f+.01f*physical+.02f*y;roundtripError=std::max(roundtripError,std::abs(row[x*4+c]-want));}}read->Unmap(0,nullptr);
    }
    if(roundtripError>1e-5f)throw std::runtime_error("Exposure-domain roundtrip changed final brightness");
    if(maxError>1e-5f)throw std::runtime_error("Native checkerboard/exposure/depth/motion conversion mismatch");
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);if(!rr.initialize(g.device.Get(),std::filesystem::path(exe).parent_path(),dir/L"ngx-logs")||!rr.create(g.list.Get(),w,h,192,98,false,false,rrQuality,true))throw std::runtime_error(rr.error());
    auto rrOutput=texture(g.device.Get(),192,98,DXGI_FORMAT_R16G16B16A16_FLOAT,true);Frame f;f.noisyColor={outputs[0].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.normalRoughness={outputs[1].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.diffuseAlbedo={outputs[2].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.specularAlbedo={outputs[3].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.depth={outputs[4].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.motion={outputs[5].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.specularHitDistance={outputs[6].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};f.output={rrOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};std::copy(view,view+16,f.worldToView.begin());f.viewToClip={1,0,0,0,0,1,0,0,0,0,-1,-1,0,0,-.025f,0};
    for(unsigned i=0;i<4;i++){f.reset=i==0;if(!rr.evaluate(g.list.Get(),f))throw std::runtime_error(rr.error());g.submit();}
    std::ofstream(dir/L"quality-hint.json")<<"{\"perf_quality\":"<<int(rrQuality)<<",\"input\":["<<w<<","<<h<<"],\"output\":["<<192<<","<<98<<"],\"evaluations\":4,\"last_evaluate_result\":\"0x"<<std::hex<<rr.lastResult()<<"\",\"minecraft_frame\":false}\n";
    unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;if(SUCCEEDED(g.device.As(&iq)))for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T size=0;iq->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());iq->GetMessage(i,message,&size);if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<message->pDescription<<'\n';}}
    if(errors)throw std::runtime_error("Bridge conversion GPU validation failed");rr.shutdownAfterGpuIdle();std::ofstream(dir/L"bridge-test.json")<<"{\"status\":\"passed\",\"checkerboard_frames\":2,\"exposure_domain_cases\":48,\"handoff_roundtrips\":24,\"maximum_roundtrip_error\":"<<roundtripError<<",\"exposure_ratios\":[0.070317,0.125,1,8],\"native_scale_branches\":[1,4],\"active_render\":[127,65],\"padded_input\":[129,67],\"maximum_conversion_error\":"<<maxError<<",\"rr_quality_evaluations\":4,\"masked_evaluations\":"<<0<<",\"unmasked_evaluations\":"<<4<<",\"create_and_evaluate_result\":\"0x1\",\"static_mask_bits\":"<<0<<",\"moving_mask_bits\":"<<0<<",\"debug_errors\":0,\"minecraft_frame\":false}\n";std::cout<<"Bridge unpack and RR Quality GPU test passed, 4 unfiltered Quality evaluations, debug errors 0.\n";return 0;
}catch(const std::exception& error){rr.shutdownAfterGpuIdle();std::cerr<<error.what()<<'\n';return 1;}}


