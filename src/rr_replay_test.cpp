#include "probe_gpu.h"
#include <sstream>
#include <cstring>
// Local diagnostic only. Repeating a frozen frame tests stationary accumulation;
// it does not establish motion quality or reproduce the game's full history.
static std::vector<unsigned char> readBytes(const std::filesystem::path& p,size_t expected){
    std::ifstream in(p,std::ios::binary|std::ios::ate);if(!in||size_t(in.tellg())!=expected)throw std::runtime_error("Missing or incorrectly sized replay input");
    std::vector<unsigned char> data(expected);in.seekg(0);in.read(reinterpret_cast<char*>(data.data()),expected);if(!in)throw std::runtime_error("Replay input read failed");return data;
}
static ComPtr<ID3D12Resource> uploadBytes(Gpu& g,ID3D12Resource* dst,const std::vector<unsigned char>& data,unsigned bpp){
    auto desc=dst->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);
    auto up=buffer(g.device.Get(),size,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped=nullptr;check(up->Map(0,nullptr,&mapped),"Upload map");
    for(unsigned y=0;y<desc.Height;y++)memcpy(static_cast<char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,data.data()+size_t(y)*desc.Width*bpp,size_t(desc.Width)*bpp);up->Unmap(0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=dst;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.pResource=up.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={dst,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};g.list->ResourceBarrier(1,&b);return up;
}
int wmain(int argc,wchar_t** argv){Backend rr;Gpu g;try{
    if(argc!=3&&argc!=4)throw std::runtime_error("Usage: rr_replay_test capture-directory output-directory [sequence-count]");
    unsigned count=argc==4?std::stoul(argv[3]):1;if(!count||count>8)throw std::runtime_error("Invalid sequence count");
    const std::filesystem::path base=argv[1],input=count>1?base/L"0":base,result=argv[2];std::filesystem::create_directories(result);
    unsigned iw=0,ih=0,ow=0,oh=0,quality=0;Frame f;
    std::ifstream params(input/L"replay-params.txt");params>>iw>>ih>>ow>>oh>>quality>>f.jitterX>>f.jitterY>>f.motionScaleX>>f.motionScaleY>>f.deltaMs;
    if(!params||!iw||!ih||iw>8192||ih>8192||ow>8192||oh>8192||!ow||!oh)throw std::runtime_error("Invalid replay metadata");
    for(auto item:{std::pair{L"rr-input-view-matrix.bin",&f.worldToView},std::pair{L"rr-input-projection-matrix.bin",&f.viewToClip}}){auto bytes=readBytes(input/item.first,64);memcpy(item.second->data(),bytes.data(),64);}
    ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(d.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device))))break;}
    if(!g.device)throw std::runtime_error("No NVIDIA D3D12 device");
    D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);if(!rr.initialize(g.device.Get(),std::filesystem::path(module).parent_path(),result/L"ngx-logs"))throw std::runtime_error(rr.error());
    if(!rr.create(g.list.Get(),iw,ih,ow,oh,false,false,static_cast<NVSDK_NGX_PerfQuality_Value>(quality),true))throw std::runtime_error(rr.error());
    const wchar_t* names[]={L"colour",L"normal-roughness",L"diffuse-albedo",L"specular-albedo",L"depth",L"motion",L"hit-distance"};
    const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_FLOAT};const unsigned bpp[]={8,8,8,8,4,4,4};
    std::vector<std::array<ComPtr<ID3D12Resource>,7>> textures(count);std::vector<Frame> frames(count,f);std::vector<ComPtr<ID3D12Resource>> uploads;
    for(unsigned sample=0;sample<count;sample++){
        auto folder=count>1?base/std::to_wstring(sample):input;auto& current=frames[sample];unsigned sw=0,sh=0,sow=0,soh=0,sq=0;
        std::ifstream parameters(folder/L"replay-params.txt");parameters>>sw>>sh>>sow>>soh>>sq>>current.jitterX>>current.jitterY>>current.motionScaleX>>current.motionScaleY>>current.deltaMs;
        if(!parameters||sw!=iw||sh!=ih||sow!=ow||soh!=oh||sq!=quality)throw std::runtime_error("Sequence creation parameters differ");
        for(auto item:{std::pair{L"rr-input-view-matrix.bin",&current.worldToView},std::pair{L"rr-input-projection-matrix.bin",&current.viewToClip}}){auto bytes=readBytes(folder/item.first,64);memcpy(item.second->data(),bytes.data(),64);}
        for(unsigned i=0;i<7;i++){textures[sample][i]=texture(g.device.Get(),iw,ih,formats[i]);auto data=readBytes(folder/(std::wstring(L"rr-input-")+names[i]+L".bin"),size_t(iw)*ih*bpp[i]);uploads.push_back(uploadBytes(g,textures[sample][i].Get(),data,bpp[i]));}
    }
    auto output=texture(g.device.Get(),ow,oh,DXGI_FORMAT_R16G16B16A16_FLOAT,true);g.submit();uploads.clear();
    for(unsigned sample=0;sample<count;sample++){auto& current=frames[sample];auto t=[&](unsigned i){return Texture{textures[sample][i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};};current.noisyColor=t(0);current.normalRoughness=t(1);current.diffuseAlbedo=t(2);current.specularAlbedo=t(3);current.depth=t(4);current.motion=t(5);current.specularHitDistance=t(6);current.output={output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS};}
    auto desc=output->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 size=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&size);auto read=buffer(g.device.Get(),size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    for(unsigned frame=0;frame<256;frame++){
        f=frames[frame%count];
        f.reset=frame==0;if(!rr.evaluate(g.list.Get(),f))throw std::runtime_error(rr.error());
        if(frame==0||frame==3||frame==15||frame==31||frame==127||frame==255){
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);
            D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);g.list->ResourceBarrier(1,&b);g.submit();
            void* mapped=nullptr;check(read->Map(0,nullptr,&mapped),"Output map");std::ofstream out(result/(L"replay-"+std::to_wstring(frame+1)+L".rgba16f"),std::ios::binary);for(unsigned y=0;y<oh;y++)out.write(static_cast<char*>(mapped)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,size_t(ow)*8);read->Unmap(0,nullptr);
        }else g.submit();
    }
    rr.shutdownAfterGpuIdle();unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;check(g.device.As(&iq),"InfoQueue");for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<unsigned char> bytes(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());iq->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;std::cerr<<m->pDescription<<'\n';}}
    if(errors)throw std::runtime_error("Replay D3D12 validation errors");std::ofstream(result/L"replay-result.json")<<"{\"evaluations\":256,\"debug_errors\":0,\"captured_frames\":"<<count<<",\"consecutive_live_frames\":false}";std::cout<<"Replay complete: 256 evaluations using "<<count<<" captured frames, zero D3D12 errors.\n";return 0;
}catch(const std::exception& e){rr.shutdownAfterGpuIdle();std::cerr<<e.what()<<'\n';return 1;}}
