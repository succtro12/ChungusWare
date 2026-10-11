#include "transparency_resolve.h"
#include "transparency_shader.h"
#include <d3dcompiler.h>
#include <cstring>
namespace bedrock_rr {
bool TransparencyResolve::initialize(ID3D12Device* device){
 Microsoft::WRL::ComPtr<ID3DBlob> shader,errors,serialized;
 if(FAILED(D3DCompile(transparencyShader,strlen(transparencyShader),"transparency_resolve.hlsl",nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&shader,&errors))){error=errors?std::string(static_cast<char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Transparency shader compile failed";return false;}
 D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,6,0,0,9}};
 D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,24};
 D3D12_ROOT_SIGNATURE_DESC desc{2,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
 if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))||FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root_)))){error="Transparency root creation failed";return false;}
 D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=root_.Get();ps.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
 if(FAILED(device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline_)))){error="Transparency pipeline creation failed";return false;}return true;
}
void TransparencyResolve::record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,unsigned w,unsigned h,unsigned fw,unsigned frame,const float* view,bool edge){
 unsigned c[24]={w,h,fw,frame,0,edge?1u:0u};memcpy(c+8,view,64);
 list->SetDescriptorHeaps(1,&heap);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());list->SetComputeRoot32BitConstants(1,24,c,0);list->Dispatch((w+7)/8,(h+7)/8,1);
 D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;list->ResourceBarrier(1,&b);
}
}
