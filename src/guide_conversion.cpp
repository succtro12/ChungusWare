#include "guide_conversion.h"
#include "device_identity.h"
#include "guide_shader.h"
#include <d3dcompiler.h>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace bedrock_rr {
bool GuideConversion::initialize(ID3D12Device* device) {
    if(!device){error_="No guide conversion device";return false;}
    ComPtr<ID3DBlob> shader,errors,serialized;
    if(FAILED(D3DCompile(bedrockGuideShader,strlen(bedrockGuideShader),"guide_conversion.hlsl",nullptr,nullptr,
                        "main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors))) {
        error_=errors?std::string(static_cast<char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Guide shader compilation failed";return false;
    }
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0};
    ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,4};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable={2,ranges};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants={0,0,2};
    D3D12_ROOT_SIGNATURE_DESC desc{2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))||
       FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_)))) {
        error_="Guide root signature creation failed";return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();pso.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    if(FAILED(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline_)))) {error_="Guide pipeline creation failed";return false;}
    device_=device;return true;
}
bool GuideConversion::record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,
                             D3D12_GPU_DESCRIPTOR_HANDLE table,unsigned width,unsigned height) {
    if(!pipeline_||!list||!heap||!width||!height||width>16384||height>16384){error_="Invalid guide dispatch";return false;}
    ComPtr<ID3D12Device> listDevice,heapDevice;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&listDevice)))||FAILED(heap->GetDevice(IID_PPV_ARGS(&heapDevice)))||
       !sameDevice(listDevice.Get(),device_.Get())||!sameDevice(heapDevice.Get(),device_.Get())){error_="Guide device mismatch";return false;}
    auto desc=heap->GetDesc();
    if(desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV||!(desc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)){
        error_="Guide descriptors need a visible CBV/SRV/UAV heap";return false;
    }
    auto base=heap->GetGPUDescriptorHandleForHeapStart().ptr;
    auto stride=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if(desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV||!(desc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)||
       table.ptr<base||(table.ptr-base)%stride||desc.NumDescriptors<7||
       (table.ptr-base)/stride>desc.NumDescriptors-7){error_="Guide descriptor table requires seven valid contiguous descriptors";return false;}
    unsigned dimensions[]={width,height};
    list->SetDescriptorHeaps(1,&heap);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());
    list->SetComputeRootDescriptorTable(0,table);list->SetComputeRoot32BitConstants(1,2,dimensions,0);
    list->Dispatch((width+7)/8,(height+7)/8,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&barrier);
    return true;
}
}
