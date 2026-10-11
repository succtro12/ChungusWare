#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
namespace bedrock_rr {
class TransparencyResolve {
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
public:
 std::string error;
 bool initialize(ID3D12Device*);
 void record(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,unsigned,unsigned,unsigned,unsigned,const float*,bool);
};
}
