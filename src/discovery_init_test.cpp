#include "probe_gpu.h"
int wmain(int argc,wchar_t** argv){
    try{
        std::filesystem::path dir=argc>1?argv[1]:L".";std::filesystem::create_directories(dir);
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto runtime=std::filesystem::path(exe).parent_path();
        auto discovery=LoadLibraryW((runtime/L"bedrock_rr_discovery.dll").c_str());
        auto start=discovery?reinterpret_cast<bool(*)()>(GetProcAddress(discovery,"NgxDiscoveryStart")):nullptr;
        if(!start||!start())throw std::runtime_error("Discovery initialization failed");
        ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");
        ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
        for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){
            DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
        if(!device)throw std::runtime_error("NVIDIA device unavailable");
        NVSDK_NGX_FeatureCommonInfo info{};std::wstring path=runtime.wstring();const wchar_t* paths[]={path.c_str()};info.PathListInfo={paths,1};
        // Numeric ID 0 is confined to this isolated ABI test process. Bedrock's
        // actual application ID is never replaced or borrowed by this test.
        auto init=NVSDK_NGX_D3D12_Init(0,(dir/L"ngx-logs").c_str(),device.Get(),&info);
        if(NVSDK_NGX_FAILED(init))throw std::runtime_error("Numeric-ID NGX Init failed");
        NVSDK_NGX_Parameter* p=nullptr;auto caps=NVSDK_NGX_D3D12_GetCapabilityParameters(&p);int available=0;
        auto query=p?p->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&available):NVSDK_NGX_Result_FAIL_NotInitialized;
        std::ofstream(dir/L"init-test.json")<<"{\"init\":"<<unsigned(init)<<",\"capabilities\":"<<unsigned(caps)<<",\"rr_query\":"<<unsigned(query)<<",\"rr_available\":"<<available<<"}\n";
        if(p)NVSDK_NGX_D3D12_DestroyParameters(p);NVSDK_NGX_D3D12_Shutdown1(device.Get());
        if(NVSDK_NGX_FAILED(caps)||NVSDK_NGX_FAILED(query))throw std::runtime_error("Native Init_Ext support query failed in ABI test");
        std::cout<<"Numeric-ID native Init_Ext ABI test passed; unregistered ID 0 provider availability recorded.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
