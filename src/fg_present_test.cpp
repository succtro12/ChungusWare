#include "probe_gpu.h"
#include "frame_generation.h"
#include "device_identity.h"
#include <DirectXMath.h>
#include <array>
void fgTestBind(IDXGISwapChain3*,ID3D12CommandQueue*,const wchar_t*);
HRESULT fgTestPresent(IDXGISwapChain3*);
int wmain(int argc,wchar_t** argv){
 try{
    auto dir=std::filesystem::path(argc>1?argv[1]:L"fg-present-test");std::filesystem::create_directories(dir);
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    Gpu g;ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&g.device)),"Device");D3D12_COMMAND_QUEUE_DESC q{};check(g.device->CreateCommandQueue(&q,IID_PPV_ARGS(&g.queue)),"Queue");check(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&g.allocator)),"Allocator");check(g.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.allocator.Get(),nullptr,IID_PPV_ARGS(&g.list)),"List");check(g.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&g.fence)),"Fence");
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto path=std::filesystem::path(exe).parent_path().wstring();const wchar_t* paths[]={path.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};auto init=NVSDK_NGX_D3D12_Init_with_ProjectID("f68a0a36-9374-4e5b-b62f-d55f5d6c7810",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"test",(dir/L"ngx-logs").c_str(),g.device.Get(),&info);if(NVSDK_NGX_FAILED(init))throw std::runtime_error("NGX init failed");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BedrockFGTest";RegisterClassW(&wc);HWND hwnd=CreateWindowW(wc.lpszClassName,L"Frame Generation GPU Test",WS_OVERLAPPEDWINDOW,0,0,960,540,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(hwnd,SW_SHOW);auto ft=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);AttachThreadInput(GetCurrentThreadId(),ft,TRUE);SetForegroundWindow(hwnd);SetFocus(hwnd);AttachThreadInput(GetCurrentThreadId(),ft,FALSE);
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=960;desc.Height=540;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferCount=3;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;ComPtr<IDXGISwapChain1> swap;check(factory->CreateSwapChainForHwnd(g.queue.Get(),hwnd,&desc,nullptr,nullptr,&swap),"Swapchain");ComPtr<IDXGISwapChain3> chain;check(swap.As(&chain),"Swapchain3");
    constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};ComPtr<ID3D12CommandQueue> rawQueue;ComPtr<IDXGISwapChain3> rawChain;ComPtr<IUnknown> obj;
    if(SUCCEEDED(g.queue->QueryInterface(unwrapped,reinterpret_cast<void**>(obj.GetAddressOf()))))check(obj.As(&rawQueue),"Raw queue");else rawQueue=g.queue;
    obj.Reset();if(SUCCEEDED(chain->QueryInterface(unwrapped,reinterpret_cast<void**>(obj.GetAddressOf()))))check(obj.As(&rawChain),"Raw chain");else rawChain=chain;
    fgTestBind(rawChain.Get(),rawQueue.Get(),(dir/L"calls.jsonl").c_str());
    auto depth=texture(g.device.Get(),192,108,DXGI_FORMAT_R32_FLOAT,true);auto motion=texture(g.device.Get(),192,108,DXGI_FORMAT_R16G16_FLOAT,true);
    for(auto r:{depth.Get(),motion.Get()}){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST};g.list->ResourceBarrier(1,&b);}
    std::vector<float> depths(192*108,2.f),zeros(192*108,0.f);auto up1=upload(g,depth.Get(),depths,1),up2=upload(g,motion.Get(),zeros,1);g.submit();
    auto transition=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};g.list->ResourceBarrier(1,&x);};
    std::array<unsigned char,1792> camera{};using namespace DirectX;auto put=[&](unsigned offset,FXMMATRIX m){XMFLOAT4X4 v;XMStoreFloat4x4(&v,m);memcpy(camera.data()+offset,&v,64);};auto proj=XMMatrixPerspectiveFovLH(1.f,320.f/180,.1f,1000.f);put(0,XMMatrixIdentity());put(128,proj);put(192,XMMatrixInverse(nullptr,proj));put(256,XMMatrixIdentity());put(320,XMMatrixInverse(nullptr,proj));put(384,proj);
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,3};ComPtr<ID3D12DescriptorHeap> rtvs;check(g.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)),"RTV heap");
    unsigned count=argc>2?_wtoi(argv[2]):1;fgCadence(argc>3?unsigned(_wtoi(argv[3])):2);fgSelect(count);auto begin=std::chrono::steady_clock::now();
    for(unsigned frame=1;frame<=120;frame++){
        transition(depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);transition(motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        fgCapture(g.list.Get(),depth.Get(),motion.Get(),camera.data(),192,108,frame);
        transition(depth.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);transition(motion.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ComPtr<ID3D12Resource> bb;check(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&bb)),"Backbuffer");auto handle=rtvs->GetCPUDescriptorHandleForHeapStart();g.device->CreateRenderTargetView(bb.Get(),nullptr,handle);transition(bb.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);float clear[]={.2f+frame*.01f,.3f,.4f,1};g.list->ClearRenderTargetView(handle,clear,0,nullptr);transition(bb.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);ID3D12CommandList* lists[]={g.list.Get()};g.submit();fgSubmitted(rawQueue.Get(),1,lists);Sleep(10);MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}check(fgTestPresent(chain.Get()),"Present");
    }
    ComPtr<ID3D12InfoQueue> iq;unsigned errors=0;if(SUCCEEDED(g.device.As(&iq)))for(UINT64 i=0;i<iq->GetNumStoredMessages();i++){SIZE_T n=0;iq->GetMessage(i,nullptr,&n);std::vector<char> b(n);auto m=reinterpret_cast<D3D12_MESSAGE*>(b.data());iq->GetMessage(i,m,&n);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){errors++;std::cerr<<m->pDescription<<'\n';}}
    auto state=fgState();auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::ofstream(dir/L"fps.json")<<"{\"rendered_fps\":"<<120/seconds<<",\"submitted_fps\":"<<(120+state.presented)/seconds<<"}\n";std::ofstream(dir/L"result.json")<<"{\"evaluations\":"<<state.evaluations<<",\"presented\":"<<state.presented<<",\"debug_errors\":"<<errors<<",\"status\":\""<<state.status<<"\"}\n";fgSelect(0);g.submit();DestroyWindow(hwnd);return errors||(count&&!state.evaluations)?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
