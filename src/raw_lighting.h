#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <string>
namespace bedrock_rr {
// A stable allocation set for one device and render size. The bridge records
// this immediately after the verified last raw specular producer, before the
// temporal/spatial denoisers. Caller must fence before destruction/reallocation.
class RawLightingSnapshot {
public:
    enum Channel { Diffuse, Specular, DiffuseChroma, SpecularDistance, Count };
    bool initialize(ID3D12Device* device,unsigned width,unsigned height);
    bool record(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,Count>& sources);
    bool record(ID3D12GraphicsCommandList* list,const std::array<ID3D12Resource*,Count>& sources,const std::array<D3D12_RESOURCE_STATES,Count>& sourceStates);
    ID3D12Resource* resource(Channel channel) const { return textures_[channel].Get(); }
    const std::string& error() const { return error_; }
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,Count> textures_;
    std::string error_;
};
}
