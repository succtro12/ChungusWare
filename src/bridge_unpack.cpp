#include "bridge_unpack.h"
#include "bridge_unpack_shader.h"
#include <d3dcompiler.h>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace bedrock_rr {
bool BridgeUnpack::initialize(ID3D12Device* device){
    ComPtr<ID3DBlob> shader,errors,serialized;
    if(FAILED(D3DCompile(bridgeUnpackShader,strlen(bridgeUnpackShader),"bridge_unpack.hlsl",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors))){error=errors?std::string(static_cast<char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Unpack shader compile failed";return false;}
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,8,0,0,9}};
    D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,ranges};
    p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,24};
    D3D12_ROOT_SIGNATURE_DESC desc{2,p,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))||FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_)))){error="Unpack root signature failed";return false;}
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root_.Get();ps.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    if(FAILED(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline_)))){error="Unpack pipeline failed";return false;}
    D3D_SHADER_MACRO macros[]={{"BRIDGE_HANDOFF","1"},{nullptr,nullptr}};
    if(FAILED(D3DCompile(bridgeUnpackShader,strlen(bridgeUnpackShader),"bridge_handoff.hlsl",macros,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors))){error=errors?std::string(static_cast<char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Handoff shader compile failed";return false;}
    D3D12_DESCRIPTOR_RANGE handoffRanges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};
    p[0].DescriptorTable={2,handoffRanges};p[1].Constants.Num32BitValues=2;
    if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))||FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&handoffRoot_)))){error="Handoff root signature failed";return false;}
    ps.pRootSignature=handoffRoot_.Get();ps.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    if(FAILED(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&handoffPipeline_)))){error="Handoff pipeline failed";return false;}
    return true;
}
void BridgeUnpack::record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,D3D12_GPU_DESCRIPTOR_HANDLE table,unsigned w,unsigned h,unsigned fw,unsigned frame,unsigned mode,float scale,const float* view,unsigned sceneDomain,bool extendOddEdge){
    unsigned constants[24]={w,h,fw,frame,mode};memcpy(constants+5,&scale,4);constants[6]=sceneDomain;constants[7]=extendOddEdge?1u:0u;memcpy(constants+8,view,64);
    list->SetDescriptorHeaps(1,&heap);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());
    list->SetComputeRootDescriptorTable(0,table);list->SetComputeRoot32BitConstants(1,24,constants,0);list->Dispatch((w+7)/8,(h+7)/8,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&barrier);
}
void BridgeUnpack::handoff(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,unsigned w,unsigned h){
    unsigned dimensions[]={w,h};list->SetDescriptorHeaps(1,&heap);list->SetComputeRootSignature(handoffRoot_.Get());list->SetPipelineState(handoffPipeline_.Get());
    list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());list->SetComputeRoot32BitConstants(1,2,dimensions,0);list->Dispatch((w+7)/8,(h+7)/8,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&barrier);
}
}
