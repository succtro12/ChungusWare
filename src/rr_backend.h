#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <string>
#include "nvsdk_ngx_helpers_dlssd_d3d.h"

namespace bedrock_rr {
struct Texture {
    ID3D12Resource* resource = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};
// Supply actual, linear, pre-denoiser data. All textures cover the whole input
// resolution. The bridge, not this API, owns command-list restoration and fences.
struct Frame {
    Texture noisyColor, output, depth, motion, diffuseAlbedo, specularAlbedo;
    Texture normalRoughness, specularHitDistance;
    std::array<float,16> worldToView{}, viewToClip{};
    float jitterX = 0, jitterY = 0;
    float motionScaleX = 1, motionScaleY = 1;
    float preExposure = 1, exposureScale = 1, deltaMs = 16.667f;
    bool reset = false;
    Texture responsivityMask;
};
class Backend {
public:
    Backend() = default;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;
    bool initialize(ID3D12Device* device, const std::filesystem::path& runtimeDirectory,
                    const std::filesystem::path& logDirectory);
    bool create(ID3D12GraphicsCommandList* list, unsigned inputWidth, unsigned inputHeight,
                unsigned outputWidth, unsigned outputHeight, bool hardwareDepth, bool invertedDepth,
                NVSDK_NGX_PerfQuality_Value quality=NVSDK_NGX_PerfQuality_Value_MaxQuality,
                bool lowResolutionMotion=false);
    bool evaluate(ID3D12GraphicsCommandList* list, const Frame& frame);
    // Must be called only after the GPU fence for every submitted evaluation.
    void shutdownAfterGpuIdle();
    const std::string& error() const { return error_; }
    unsigned lastResult() const { return lastResult_; }
    unsigned evaluationCount() const { return evaluations_; }
private:
    bool result(NVSDK_NGX_Result value, const char* operation);
    bool validate(const Frame& frame);
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    NVSDK_NGX_Parameter* parameters_ = nullptr;
    NVSDK_NGX_Handle* feature_ = nullptr;
    bool initialized_ = false;
    unsigned iw_ = 0, ih_ = 0, ow_ = 0, oh_ = 0;
    unsigned lastResult_ = 0, evaluations_ = 0;
    std::string error_;
};
}
