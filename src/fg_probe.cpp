#include "probe_gpu.h"
#include "nvsdk_ngx_helpers_dlssg_d3d.h"
int wmain(int argc,wchar_t** argv) {
    auto dir=std::filesystem::path(argc>1?argv[1]:L"fg-test");
    std::filesystem::create_directories(dir);
    std::ofstream log(dir/L"calls.jsonl");
    auto record=[&](const char* op,NVSDK_NGX_Result r,int value=-1){log<<"{\"call\":\""<<op<<"\",\"result\":\"0x"<<std::hex<<unsigned(r)<<std::dec<<"\",\"value\":"<<value<<"}\n";log.flush();};
    Gpu g; NVSDK_NGX_Parameter* p=nullptr; NVSDK_NGX_Handle* feature=nullptr;bool initialized=false;
    try {
        ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");
        ComPtr<IDXGIAdapter1> adapter;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
            if(desc.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device))))break;adapter.Reset();}
        if(!g.device)throw std::runtime_error("No NVIDIA device");
        D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");
        check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");
        check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");
        check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto runtime=std::filesystem::path(exe).parent_path().wstring();const wchar_t* paths[]={runtime.c_str()};
        NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};
        auto r=NVSDK_NGX_D3D12_Init_with_ProjectID("f68a0a36-9374-4e5b-b62f-d55f5d6c7810",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"0.1.0",(dir/L"ngx-logs").c_str(),g.device.Get(),&info);record("Init_ProjectID",r);
        if(NVSDK_NGX_FAILED(r))throw std::runtime_error("NGX init failed");initialized=true;
        r=NVSDK_NGX_D3D12_GetCapabilityParameters(&p);record("GetCapabilityParameters",r);if(!p)throw std::runtime_error("No capabilities");
        for(const char* key:{NVSDK_NGX_Parameter_FrameGeneration_Available,NVSDK_NGX_Parameter_FrameGeneration_FeatureInitResult,NVSDK_NGX_Parameter_FrameGeneration_NeedsUpdatedDriver,NVSDK_NGX_DLSSG_Parameter_MultiFrameCountMax}){int v=-1;r=p->Get(key,&v);record(key,r,v);}
        NVSDK_NGX_DLSSG_Create_Params creation{};creation.Width=960;creation.Height=540;creation.RenderWidth=640;creation.RenderHeight=360;creation.NativeBackbufferFormat=DXGI_FORMAT_R32G32B32A32_FLOAT;
        r=NGX_D3D12_CREATE_DLSSG(g.list.Get(),1,1,&feature,p,&creation);record("NGX_D3D12_CREATE_DLSSG(feature=11)",r);g.submit();
        if(NVSDK_NGX_FAILED(r))throw std::runtime_error("FG creation failed");
        auto color=texture(g.device.Get(),960,540,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto depth=texture(g.device.Get(),640,360,DXGI_FORMAT_R32_FLOAT);auto motion=texture(g.device.Get(),640,360,DXGI_FORMAT_R32G32_FLOAT);
        auto output=texture(g.device.Get(),960,540,DXGI_FORMAT_R32G32B32A32_FLOAT,true);
        std::vector<float> colors(960*540*4,0.3f),depths(640*360,.5f),mvs(640*360*2,0.f);
        std::vector<ComPtr<ID3D12Resource>> uploads;uploads.push_back(upload(g,color.Get(),colors,4));uploads.push_back(upload(g,depth.Get(),depths,1));uploads.push_back(upload(g,motion.Get(),mvs,2));g.submit();uploads.clear();
        NVSDK_NGX_D3D12_DLSSG_Eval_Params eval{};eval.pBackbuffer=color.Get();eval.pDepth=depth.Get();eval.pMVecs=motion.Get();eval.pOutputInterpFrame=output.Get();
        NVSDK_NGX_DLSSG_Opt_Eval_Params opt{};
        for(int i=0;i<4;i++)opt.cameraViewToClip[i][i]=opt.clipToCameraView[i][i]=opt.clipToLensClip[i][i]=opt.clipToPrevClip[i][i]=opt.prevClipToClip[i][i]=1.f;
        opt.cameraNear=.1f;opt.cameraFar=1000.f;opt.cameraFOV=1.f;opt.cameraAspectRatio=960.f/540;opt.cameraMotionIncluded=true;opt.cameraUp[1]=1;opt.cameraRight[0]=1;opt.cameraFwd[2]=1;opt.mvecScale[0]=opt.mvecScale[1]=1;opt.depthSubrectSize=opt.mvecsSubrectSize={640,360};opt.backbufferSubrectSize=opt.outputInterpSubrectSize={960,540};
        for(unsigned frame=0;frame<4;frame++){opt.reset=frame==0;r=NGX_D3D12_EVALUATE_DLSSG(g.list.Get(),feature,p,&eval,&opt);record("NGX_D3D12_EVALUATE_DLSSG(synthetic)",r,int(frame));g.submit();if(NVSDK_NGX_FAILED(r))throw std::runtime_error("FG evaluation failed");}
        ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(SUCCEEDED(g.device.As(&iq)))for(UINT64 i=0;i<iq->GetNumStoredMessages();i++){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<char> b(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(b.data());iq->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<m->pDescription<<'\n';}}
        record("GPU_completed_debug_errors",NVSDK_NGX_Result_Success,errors);
        NVSDK_NGX_D3D12_ReleaseFeature(feature);NVSDK_NGX_D3D12_DestroyParameters(p);NVSDK_NGX_D3D12_Shutdown1(g.device.Get());return errors?2:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(feature)NVSDK_NGX_D3D12_ReleaseFeature(feature);if(p)NVSDK_NGX_D3D12_DestroyParameters(p);if(initialized)NVSDK_NGX_D3D12_Shutdown1(g.device.Get());return 1;}
}
