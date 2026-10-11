#include "probe_gpu.h"
#include "transparency_resolve.h"
#include <limits>
int wmain(int argc,wchar_t** argv){try{
 auto dir=std::filesystem::path(argc>1?argv[1]:L"transparency-test");std::filesystem::create_directories(dir);
 ComPtr<ID3D12Debug> debug;check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug");debug->EnableDebugLayer();
 Gpu g;ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device)),"Device");
 D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
 TransparencyResolve resolve;if(!resolve.initialize(g.device.Get()))throw std::runtime_error(resolve.error);
 unsigned cases=0;float maxError=0;constexpr unsigned w=17,h=7,aw=18,fw=8;float view[16]{};for(unsigned i=0;i<4;i++)view[i*5]=1;
 for(unsigned impulse=0;impulse<5;impulse++)for(unsigned depthCase=0;depthCase<4;depthCase++)for(bool gradient:{false,true})for(bool opaque:{false,true})for(float weight:{0.f,.25f,.5f,.75f,1.f,-.25f,1.25f,std::numeric_limits<float>::quiet_NaN()})for(unsigned frame=0;frame<2;frame++){
  float leftDepth=depthCase==1?30.f:12.f;
  float rightDepth=depthCase==1||depthCase==3?12.f:depthCase==2?65504.f:30.f;
  std::array<ComPtr<ID3D12Resource>,9> inputs;std::array<ComPtr<ID3D12Resource>,6> outputs;std::vector<ComPtr<ID3D12Resource>> uploads;
  for(unsigned i=0;i<9;i++){
   unsigned channels=i==4||i==5||i==8?1:4;unsigned iw=i==8?1:aw,ih=i==8?1:h;
   inputs[i]=texture(g.device.Get(),iw,ih,channels==1?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
   std::vector<float> data(iw*ih*channels);
   for(unsigned y=0;y<ih;y++)for(unsigned x=0;x<iw;x++)for(unsigned c=0;c<channels;c++){
    bool right=x>=fw;float value=0;
    if(i==0)value=c==3?(opaque?0.f:4096.f):float(2+2*c+(right?8:0))+(gradient?(.125f*float(x%fw)+.25f*y):0.f);
    if(i==0&&c<3&&right&&impulse!=0&&((impulse==2&&x>=fw+3&&x<=fw+5&&y>=2&&y<=4)||(x==fw+4&&y==3)||(impulse==4&&x==fw+5&&y==3)))value=1024.f;
    if(i==1)value=c==1?(right?-1.f:1.f):c==3?(right?.8f:.05f):0.f;
    if(i==2)value=c==3?1.f:right?.8f:.3f;
    if(i==3)value=c==3?1.f:right?.2f:.04f;
    if(i==4)value=right?rightDepth:leftDepth;
    if(i==4&&right&&impulse==3&&((y==3&&(x==fw+3||x==fw+5))||(x==fw+4&&(y==2||y==4))))value=rightDepth*2;
    if(i==5)value=right?128.f:7.f;
    if(i==6)value=c==2?-1.f:0.f;
    if(i==7)value=c==3?(right?-1.f:weight):0.f;
    if(i==8)value=.5f;
    data[(y*iw+x)*channels+c]=value;
   }
   uploads.push_back(upload(g,inputs[i].Get(),data,channels));
  }
  for(unsigned i=0;i<6;i++){
   unsigned channels=i>=4?1:4;outputs[i]=texture(g.device.Get(),w,h,channels==1?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT,true);
   D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={outputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};g.list->ResourceBarrier(1,&b);
   uploads.push_back(upload(g,outputs[i].Get(),std::vector<float>(w*h*channels,-123.f),channels));b.Transition.StateBefore=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;g.list->ResourceBarrier(1,&b);
  }
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=15;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;ComPtr<ID3D12DescriptorHeap> heap;check(g.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Heap");auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto stride=g.device->GetDescriptorHandleIncrementSize(hd.Type);
  for(auto& input:inputs){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=input->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;g.device->CreateShaderResourceView(input.Get(),&v,cpu);cpu.ptr+=stride;}
  for(auto& output:outputs){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=output->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;g.device->CreateUnorderedAccessView(output.Get(),nullptr,&v,cpu);cpu.ptr+=stride;}
  resolve.record(g.list.Get(),heap.Get(),w,h,fw,frame,view,true);g.submit();
  bool changed=!opaque&&std::isfinite(weight)&&weight>=0&&weight<=1;bool right=weight>.5f;
  for(unsigned i=0;i<6;i++){
   auto desc=outputs[i]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;g.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&bytes);auto read=buffer(g.device.Get(),bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
   D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={outputs[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};g.list->ResourceBarrier(1,&b);D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=read.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;from.pResource=outputs[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;g.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);g.submit();void* data=nullptr;check(read->Map(0,nullptr,&data),"Readback");unsigned channels=i>=4?1:4;
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<channels;c++){
    float want=-123.f;
    unsigned sampleX=std::min(x,(w&~1u)-1u)>>1u;
    float pixelRightDepth=impulse==3&&((y==3&&(sampleX==3||sampleX==5))||(sampleX==4&&(y==2||y==4)))?rightDepth*2:rightDepth;
    bool right=weight>.5f;
    if(changed){
     if(i==0){
      unsigned px=std::min(x,(w&~1u)-1u),qx=(px>>1u)+(((px^y)&1u)==((frame&1u)^1u)?0u:fw);
      unsigned ox=qx<fw?qx+fw:qx-fw,lo=ox<fw?0u:fw,hi=lo+fw-1;
      int dx=int((x<<1u)&2u)-1;
      auto rawSample=[&](unsigned sx,unsigned sy){
       if(impulse!=0&&sx>=fw&&((impulse==2&&sx>=fw+3&&sx<=fw+5&&sy>=2&&sy<=4)||(sx==fw+4&&sy==3)||(impulse==4&&sx==fw+5&&sy==3)))return 1024.f;
       return float(2+2*c+(sx>=fw?8:0))+(gradient?(.125f*float(sx%fw)+.25f*sy):0.f);
      };
      auto sample=[&](unsigned sx,unsigned sy){
       float v=rawSample(sx,sy);
       // Explicit isolated/pair fixtures: trim the highest neighbour from the
       // eight-sample mean. Sky, 3x3 clusters and four-depth edges are preserved.
       if(rightDepth<65504&&((impulse==1&&sx==fw+4&&sy==3)||(impulse==4&&(sx==fw+4||sx==fw+5)&&sy==3))){
        float sum=0,largest=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)if(dx||dy){float n=rawSample(unsigned(int(sx)+dx),unsigned(int(sy)+dy));sum+=n;largest=std::max(largest,n);}return (sum-largest)/7;
       }return v;
      };
      float paired=.25f*(sample(ox,y?y-1:0)+sample(ox,std::min(y+1,h-1))+sample(ox,y)+sample(unsigned(std::clamp(int(ox)+dx,int(lo),int(hi))),y));
      float alpha=qx<fw?weight:1-weight;
      want=c==3?1.f:2*(sample(qx,y)*(1-alpha)+paired*alpha);
     }
     if(i==1)want=c==1?(right?-1.f:1.f):c==3?(right?.8f:.05f):0.f;
     if(i==2)want=c==3?1.f:right?.8f:.3f;
     if(i==3)want=c==3?1.f:right?.2f:.04f;
     if(i==4)want=right?(pixelRightDepth>=65504?1e6:pixelRightDepth):leftDepth;
     if(i==5)want=right?128.f:7.f;
    }
    float actual=reinterpret_cast<float*>(static_cast<char*>(data)+fp.Offset+size_t(y)*fp.Footprint.RowPitch)[x*channels+c];maxError=std::max(maxError,std::abs(actual-want));
   }read->Unmap(0,nullptr);
  }++cases;
 }
 unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;check(g.device.As(&iq),"InfoQueue");for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<unsigned char> data(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(data.data());iq->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<m->pDescription<<'\n';}}
 if(errors||maxError>1e-5f)throw std::runtime_error("Transparency resolve validation failed");
 std::ofstream(dir/L"transparency-test.json")<<"{\"status\":\"passed\",\"cases\":"<<cases<<",\"maximum_error\":"<<maxError<<",\"opaque_unchanged\":true,\"invalid_weights_rejected\":true,\"opposite_phase_consistent\":true,\"varying_colour_fixture\":true,\"odd_width\":17,\"debug_errors\":0}";
 std::cout<<"Transparency resolve passed "<<cases<<" GPU cases; opaque unchanged, phase consistency, invalid weights, odd edge; debug errors 0.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
