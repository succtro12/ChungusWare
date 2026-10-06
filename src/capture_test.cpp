#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <thread>
#include "hook_slots.h"
#include "bridge_capture.h"
using Microsoft::WRL::ComPtr;
static void check(HRESULT r,const char* name){if(FAILED(r)){std::ostringstream s;s<<name<<" failed 0x"<<std::hex<<unsigned(r);throw std::runtime_error(s.str());}}
static ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;
    d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;if(uav)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)),"Buffer");return r;
}
int wmain(int argc,wchar_t** argv){try{
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto dir=std::filesystem::path(exe).parent_path();
    auto reportDir=argc>1?std::filesystem::path(argv[1]):dir;std::filesystem::create_directories(reportDir);
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    bool proxy=argc>2&&!wcscmp(argv[2],L"--proxy");
    if(!proxy&&!LoadLibraryW((dir/L"bedrock_rr_capture.dll").c_str()))throw std::runtime_error("Capture DLL failed to load");
    auto wait=reinterpret_cast<BOOL(WINAPI*)(DWORD)>(GetProcAddress(GetModuleHandleW(L"bedrock_rr_capture.dll"),"BedrockRrWaitUntilReady"));
    bool ready=proxy||(wait&&wait(10000));
    if(!ready)throw std::runtime_error("Capture hooks not ready");
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device5> d;
    if(proxy&&!GetModuleHandleW(L"bedrock_rr_capture.dll"))throw std::runtime_error("DXGI bootstrap did not load capture");
    for(UINT i=0;factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))!=DXGI_ERROR_NOT_FOUND;i++){
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);if(desc.VendorId==0x10de&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))))break;adapter.Reset();}
    if(!d)throw std::runtime_error("NVIDIA device unavailable");
    bool profile=argc>4&&!wcscmp(argv[2],L"--profile");
    ComPtr<ID3D12RootSignature> profileRoot;ComPtr<ID3D12PipelineState> profilePso;
    if(profile){
        auto read=[](const wchar_t* path){std::ifstream f(std::filesystem::path(path),std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});if(bytes.empty())throw std::runtime_error("Profile fixture missing");return bytes;};
        auto roots=read(argv[3]),shader=read(argv[4]);
        check(d->CreateRootSignature(0,roots.data(),roots.size(),IID_PPV_ARGS(&profileRoot)),"Profile root");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=profileRoot.Get();desc.CS={shader.data(),shader.size()};
        check(d->CreateComputePipelineState(&desc,IID_PPV_ARGS(&profilePso)),"Profile PSO");
    }
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(d->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"Queue");
    ComPtr<ID3D12CommandAllocator> allocator;check(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
    ComPtr<ID3D12GraphicsCommandList4> list;check(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
    if(profile){
        auto query=reinterpret_cast<bool(*)(ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES*)>(GetProcAddress(GetModuleHandleW(L"bedrock_rr_capture.dll"),"BedrockRrResourceState"));
        auto verify=[&](ID3D12Resource* resource){D3D12_RESOURCE_STATES state{};if(!query||!query(list.Get(),resource,&state)||state!=D3D12_RESOURCE_STATE_UNORDERED_ACCESS)throw std::runtime_error("Extended resource creation state missing");};
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=16;desc.Height=16;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Device4> device4;check(d.As(&device4),"Device4");ComPtr<ID3D12Resource> c1;
        check(device4->CreateCommittedResource1(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,nullptr,IID_PPV_ARGS(&c1)),"Committed1");verify(c1.Get());
        ComPtr<ID3D12Device8> device8;check(d.As(&device8),"Device8");D3D12_RESOURCE_DESC1 desc1{};desc1.Dimension=desc.Dimension;desc1.Width=desc.Width;desc1.Height=desc.Height;desc1.DepthOrArraySize=desc.DepthOrArraySize;desc1.MipLevels=desc.MipLevels;desc1.Format=desc.Format;desc1.SampleDesc=desc.SampleDesc;desc1.Flags=desc.Flags;
        ComPtr<ID3D12Resource> c2;check(device8->CreateCommittedResource2(&hp,D3D12_HEAP_FLAG_NONE,&desc1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,nullptr,IID_PPV_ARGS(&c2)),"Committed2");verify(c2.Get());
        auto allocation=d->GetResourceAllocationInfo(0,1,&desc);D3D12_HEAP_DESC heapDesc{};heapDesc.SizeInBytes=allocation.SizeInBytes;heapDesc.Alignment=allocation.Alignment;heapDesc.Properties=hp;heapDesc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
        ComPtr<ID3D12Heap> placedHeap;check(d->CreateHeap(&heapDesc,IID_PPV_ARGS(&placedHeap)),"Placed heap");ComPtr<ID3D12Resource> placedResource;
        check(device8->CreatePlacedResource1(placedHeap.Get(),0,&desc1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&placedResource)),"Placed1");verify(placedResource.Get());
        D3D12_RESOURCE_STATES state{};if(query(list.Get(),nullptr,&state))throw std::runtime_error("Null resource accepted");
    }
    auto output=makeBuffer(d.Get(),256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
    auto input=makeBuffer(d.Get(),1792,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped=nullptr;D3D12_RANGE empty{0,0};check(input->Map(0,&empty,&mapped),"Map");memset(mapped,0,1792);*static_cast<float*>(mapped)=3.f;
    static_cast<UINT*>(mapped)[1352/4]=4242; // Bedrock ViewCB frameCount offset
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={1,&range};params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].Descriptor={0,0};params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> serialized,error;
    check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error),"Root serialize");ComPtr<ID3D12RootSignature> root;
    check(d->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)),"Root");
    const char* source="RWStructuredBuffer<float> result:register(u0); cbuffer View:register(b0){float value;};[numthreads(1,1,1)]void main(){result[0]=value;}";
    ComPtr<ID3DBlob> cs;check(D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&cs,&error),"Compile CS");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.CS={cs->GetBufferPointer(),cs->GetBufferSize()};ComPtr<ID3D12PipelineState> compute;
    check(d->CreateComputePipelineState(&pso,IID_PPV_ARGS(&compute)),"Compute PSO");
    struct alignas(void*) RootSub{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type=D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;ID3D12RootSignature* value;};
    struct alignas(void*) CsSub{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type=D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;D3D12_SHADER_BYTECODE value;};
    struct Stream{RootSub root;CsSub cs;}stream{{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,root.Get()},{D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,pso.CS}};
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{sizeof(stream),&stream};ComPtr<ID3D12PipelineState> streamPso;
    check(d->CreatePipelineState(&streamDesc,IID_PPV_ARGS(&streamPso)),"Stream PSO");
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};ComPtr<ID3D12DescriptorHeap> heap;
    check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Heap");UINT stride=d->GetDescriptorHandleIncrementSize(hd.Type);
    D3D12_DESCRIPTOR_HEAP_DESC stagingDesc{hd.Type,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};ComPtr<ID3D12DescriptorHeap> staging;
    check(d->CreateDescriptorHeap(&stagingDesc,IID_PPV_ARGS(&staging)),"Staging heap");
    auto cpu=staging->GetCPUDescriptorHandleForHeapStart();D3D12_CPU_DESCRIPTOR_HANDLE copy1{cpu.ptr+stride},copy2=heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=64;view.Buffer.StructureByteStride=4;
    d->CreateUnorderedAccessView(output.Get(),nullptr,&view,cpu);d->CopyDescriptorsSimple(1,copy1,cpu,hd.Type);d->CopyDescriptors(1,&copy2,nullptr,1,&copy1,nullptr,hd.Type);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());
    auto gpu=heap->GetGPUDescriptorHandleForHeapStart();list->SetComputeRootDescriptorTable(0,gpu);list->SetComputeRootConstantBufferView(1,input->GetGPUVirtualAddress());
    D3D12_RESOURCE_BARRIER stateProbe{};stateProbe.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    stateProbe.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&stateProbe);
    std::swap(stateProbe.Transition.StateBefore,stateProbe.Transition.StateAfter);list->ResourceBarrier(1,&stateProbe);
    list->SetPipelineState(compute.Get());
    auto captureModule=GetModuleHandleW(L"bedrock_rr_capture.dll");
    auto clone=reinterpret_cast<BridgeCloneTables>(GetProcAddress(captureModule,"BedrockRrCloneTables"));
    auto bindClone=reinterpret_cast<BridgeBindClone>(GetProcAddress(captureModule,"BedrockRrBindClone"));
    auto preserve=reinterpret_cast<BridgeRestore>(GetProcAddress(captureModule,"BedrockRrPreserveCompute"));
    ComPtr<ID3D12DescriptorHeap> privateHeap;check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&privateHeap)),"Bridge private heap");UINT offsets[64],counts[64];
    if(!clone||!bindClone||!preserve||!clone(list.Get(),privateHeap.Get(),offsets,counts))throw std::runtime_error("Bridge native descriptor clone failed");
    struct Inject {ID3D12GraphicsCommandList* list;ID3D12DescriptorHeap* heap;BridgeBindClone bind;const UINT* offsets;} inject{list.Get(),privateHeap.Get(),bindClone,offsets};
    preserve(list.Get(),+[](void* data){auto& call=*static_cast<Inject*>(data);call.bind(call.list,call.heap,call.offsets);call.list->Dispatch(1,1,1);D3D12_RESOURCE_BARRIER ordering{};ordering.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;call.list->ResourceBarrier(1,&ordering);call.list->SetComputeRootSignature(nullptr);},&inject);
    list->Dispatch(1,1,1);D3D12_RESOURCE_BARRIER uav{};uav.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;uav.UAV.pResource=output.Get();list->ResourceBarrier(1,&uav);
    list->SetPipelineState(streamPso.Get());list->Dispatch(1,1,1);list->ResourceBarrier(1,&uav);
    // Exercise the method next to CreateStateObject: this caught the original
    // mistaken Device5 hook slot without needing to risk another game crash.
    D3D12_RAYTRACING_GEOMETRY_DESC geometry{};geometry.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;geometry.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometry.Triangles.VertexBuffer={input->GetGPUVirtualAddress(),12};geometry.Triangles.VertexCount=3;geometry.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS as{};as.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;as.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;as.NumDescs=1;as.pGeometryDescs=&geometry;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};d->GetRaytracingAccelerationStructurePrebuildInfo(&as,&prebuild);
    if(!prebuild.ResultDataMaxSizeInBytes)throw std::runtime_error("RTAS prebuild failed");
    auto libPath=dir/L"capture-test-rays.dxil";std::ifstream libFile(libPath,std::ios::binary);std::vector<char> lib((std::istreambuf_iterator<char>(libFile)),{});
    if(lib.empty())throw std::runtime_error("Ray library fixture missing");
    D3D12_EXPORT_DESC exportDesc{L"RayGen",nullptr,D3D12_EXPORT_FLAG_NONE};D3D12_DXIL_LIBRARY_DESC library{{lib.data(),lib.size()},1,&exportDesc};
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{4,8};D3D12_RAYTRACING_PIPELINE_CONFIG rayConfig{1};ID3D12RootSignature* globalRoot=root.Get();
    D3D12_STATE_SUBOBJECT subobjects[]={{D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY,&library},{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,&shaderConfig},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,&rayConfig},{D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,&globalRoot}};
    D3D12_STATE_OBJECT_DESC rayDesc{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,4,subobjects};ComPtr<ID3D12StateObject> ray;
    check(d->CreateStateObject(&rayDesc,IID_PPV_ARGS(&ray)),"Ray state object");ComPtr<ID3D12StateObjectProperties> properties;check(ray.As(&properties),"Ray properties");
    auto table=makeBuffer(d.Get(),64,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* shaderTable=nullptr;check(table->Map(0,&empty,&shaderTable),"Shader table map");
    memcpy(shaderTable,properties->GetShaderIdentifier(L"RayGen"),32);table->Unmap(0,nullptr);
    list->SetPipelineState1(ray.Get());D3D12_DISPATCH_RAYS_DESC rays{};rays.RayGenerationShaderRecord={table->GetGPUVirtualAddress(),32};rays.Width=rays.Height=rays.Depth=1;list->DispatchRays(&rays);
    list->ResourceBarrier(1,&uav);D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);
    auto readback=makeBuffer(d.Get(),256,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(readback.Get(),0,output.Get(),0,4);
    check(list->Close(),"Close");ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);ComPtr<ID3D12Fence> fence;check(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(queue->Signal(fence.Get(),1),"Signal");check(fence->SetEventOnCompletion(1,event),"Completion");
    if(WaitForSingleObject(event,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Fence timeout");CloseHandle(event);check(d->GetDeviceRemovedReason(),"Device removed");
    void* result=nullptr;D3D12_RANGE reads{0,4};check(readback->Map(0,&reads,&result),"Readback");float value=*static_cast<float*>(result);readback->Unmap(0,nullptr);input->Unmap(0,nullptr);
    if(value!=7.f)throw std::runtime_error("Capture changed GPU output");
    if(profile){
        auto resourceState=reinterpret_cast<bool(*)(ID3D12GraphicsCommandList*,ID3D12Resource*,D3D12_RESOURCE_STATES*)>(GetProcAddress(captureModule,"BedrockRrResourceState"));
        check(allocator->Reset(),"Reset completed allocator");check(list->Reset(allocator.Get(),nullptr),"Reset completed list");
        D3D12_RESOURCE_STATES inherited{};
        if(!resourceState||!resourceState(list.Get(),output.Get(),&inherited)||inherited!=D3D12_RESOURCE_STATE_COPY_SOURCE)throw std::runtime_error("Submitted resource state was not inherited");
        check(list->Close(),"Close worker submission list");
        std::thread worker([&]{ID3D12CommandList* workerLists[]={list.Get()};queue->ExecuteCommandLists(1,workerLists);});worker.join();
        HANDLE workerEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(queue->Signal(fence.Get(),2),"Worker signal");check(fence->SetEventOnCompletion(2,workerEvent),"Worker completion");if(WaitForSingleObject(workerEvent,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Worker fence timeout");CloseHandle(workerEvent);
        check(allocator->Reset(),"Reset worker allocator");check(list->Reset(allocator.Get(),nullptr),"Reset worker list");
        if(!resourceState(list.Get(),output.Get(),&inherited)||inherited!=D3D12_RESOURCE_STATE_COPY_SOURCE)throw std::runtime_error("Sequential thread handoff wrongly rejected resource state");
        D3D12_RESOURCE_BARRIER alias{};alias.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;list->ResourceBarrier(1,&alias);
        if(resourceState(list.Get(),output.Get(),&inherited))throw std::runtime_error("Wildcard alias retained inherited resource state");
        check(list->Close(),"Close state probe list");
    }
    unsigned errors=0;ComPtr<ID3D12InfoQueue> iq;
    if(SUCCEEDED(d.As(&iq)))for(UINT64 i=0;i<iq->GetNumStoredMessagesAllowedByRetrievalFilter();i++){SIZE_T size=0;iq->GetMessage(i,nullptr,&size);std::vector<char> data(size);auto msg=reinterpret_cast<D3D12_MESSAGE*>(data.data());iq->GetMessage(i,msg,&size);
        if(msg->Severity==D3D12_MESSAGE_SEVERITY_ERROR||msg->Severity==D3D12_MESSAGE_SEVERITY_CORRUPTION){errors++;std::cerr<<msg->pDescription<<'\n';}}
    if(errors)throw std::runtime_error("D3D12 validation errors");
    auto slots=BedrockRrHookSlots();std::ofstream(reportDir/L"capture-test.json")<<"{\"status\":\"passed\",\"compute_dispatches\":3,\"ray_dispatches\":1,\"rtas_prebuild_bytes\":"<<prebuild.ResultDataMaxSizeInBytes
        <<",\"stream_slot\":"<<slots->stream<<",\"state_object_slot\":"<<slots->stateObject<<",\"submitted_state_test\":"<<(profile?"true":"false")<<",\"gpu_result\":"<<value<<",\"debug_errors\":"<<errors<<"}\n";
    std::cout<<"Native capture test passed: compute, stream PSO, RTAS query, ray pipeline, ray dispatch; GPU output unchanged.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
