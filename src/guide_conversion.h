#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
namespace bedrock_rr {
// Caller owns lifetime, resource transitions, GPU fences and command-list
// restoration. Inputs must be readable, outputs must be UAVs, and must not alias.
class GuideConversion {
public:
    bool initialize(ID3D12Device* device);
    bool record(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* heap,
                D3D12_GPU_DESCRIPTOR_HANDLE table, unsigned width, unsigned height);
    const std::string& error() const { return error_; }
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    std::string error_;
};
}
