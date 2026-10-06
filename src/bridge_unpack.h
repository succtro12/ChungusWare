#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>
namespace bedrock_rr {
class BridgeUnpack {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> handoffRoot_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> handoffPipeline_;
public:
    std::string error;
    bool initialize(ID3D12Device*);
    void record(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,D3D12_GPU_DESCRIPTOR_HANDLE,unsigned,unsigned,unsigned,unsigned,unsigned,float,const float*,unsigned sceneDomain=0,bool extendOddEdge=false);
    void handoff(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,unsigned,unsigned);
};
}
