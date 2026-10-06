#include "rr_backend.h"
#include <cmath>
#include <sstream>
#include <vector>
#include <unordered_map>

namespace bedrock_rr {
bool Backend::result(NVSDK_NGX_Result value, const char* operation) {
    lastResult_ = static_cast<unsigned>(value);
    if (NVSDK_NGX_SUCCEED(value)) return true;
    std::ostringstream s;
    s << operation << " failed: NGX 0x" << std::hex << lastResult_;
    error_ = s.str();
    return false;
}
bool Backend::initialize(ID3D12Device* device, const std::filesystem::path& runtimeDir,
                         const std::filesystem::path& logDir) {
    if (!device || initialized_) { error_ = "Invalid device or already initialized"; return false; }
    std::filesystem::create_directories(logDir);
    auto path = runtimeDir.wstring();
    const wchar_t* paths[] = { path.c_str() };
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    // This project's GUID identifies this prototype; it is not Mojang's app ID.
    if (!result(NVSDK_NGX_D3D12_Init_with_ProjectID("f68a0a36-9374-4e5b-b62f-d55f5d6c7810",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM, "0.1.0", logDir.c_str(), device, &info), "NGX init")) return false;
    device_ = device;
    initialized_ = true;
    if (!result(NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters_), "RR capabilities")) return false;
    int available = 0;
    auto r = parameters_->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available, &available);
    if (NVSDK_NGX_FAILED(r) || !available) {
        error_ = "NGX reports ray reconstruction unavailable; check runtime and driver";
        return false;
    }
    return true;
}
bool Backend::create(ID3D12GraphicsCommandList* list, unsigned iw, unsigned ih,
                     unsigned ow, unsigned oh, bool hardwareDepth, bool invertedDepth,NVSDK_NGX_PerfQuality_Value quality,bool lowResolutionMotion) {
    if (!initialized_ || !parameters_ || feature_ || !list || !iw || !ih || ow < iw || oh < ih) {
        error_ = "Invalid feature creation or resize without retiring old GPU work"; return false;
    }
    Microsoft::WRL::ComPtr<ID3D12Device> listOwner;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&listOwner))) || listOwner.Get() != device_.Get()) {
        error_ = "RR command list belongs to a different device"; return false;
    }
    for (const char* name : { NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA,
            NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality,
            NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced,
            NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance,
            NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance,
            NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality }) {
        parameters_->Set(name, unsigned(NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F));
    }
    NVSDK_NGX_DLSSD_Create_Params p{};
    p.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    p.InRoughnessMode = NVSDK_NGX_DLSS_Roughness_Mode_Packed;
    p.InUseHWDepth = hardwareDepth ? NVSDK_NGX_DLSS_Depth_Type_HW : NVSDK_NGX_DLSS_Depth_Type_Linear;
    p.InWidth = iw; p.InHeight = ih; p.InTargetWidth = ow; p.InTargetHeight = oh;
    p.InPerfQualityValue = quality;
    p.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if(lowResolutionMotion)p.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (invertedDepth) p.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (!result(NGX_D3D12_CREATE_DLSSD_EXT(list, 1, 1, &feature_, parameters_, &p), "RR Preset F create")) return false;
    iw_ = iw; ih_ = ih; ow_ = ow; oh_ = oh;
    return true;
}
bool Backend::validate(const Frame& f) {
    const Texture* inputs[] = { &f.noisyColor, &f.depth, &f.motion, &f.diffuseAlbedo,
        &f.specularAlbedo, &f.normalRoughness, &f.specularHitDistance, &f.responsivityMask };
    for (auto t : inputs) {
        if(t==&f.responsivityMask&&!t->resource)continue;
        if (!t->resource || t->resource == f.output.resource) { error_ = "Missing or aliased RR input"; return false; }
        auto d = t->resource->GetDesc();
        if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != iw_ || d.Height != ih_ ||
            d.DepthOrArraySize != 1 || d.SampleDesc.Count != 1 || (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
            error_ = "RR input texture dimensions, samples or SRV access invalid"; return false;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> owner;
        if (FAILED(t->resource->GetDevice(IID_PPV_ARGS(&owner))) || owner.Get() != device_.Get()) {
            error_ = "RR input belongs to a different device"; return false;
        }
    }
    if (!f.output.resource) { error_ = "Missing RR output"; return false; }
    Microsoft::WRL::ComPtr<ID3D12Device> outputOwner;
    if (FAILED(f.output.resource->GetDevice(IID_PPV_ARGS(&outputOwner))) || outputOwner.Get() != device_.Get()) {
        error_ = "RR output belongs to a different device"; return false;
    }
    auto d = f.output.resource->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != ow_ || d.Height != oh_ ||
        d.DepthOrArraySize != 1 || d.SampleDesc.Count != 1 || !(d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
        error_ = "RR output size or UAV access invalid"; return false;
    }
    auto isColor = [](DXGI_FORMAT v) { return v == DXGI_FORMAT_R16G16B16A16_FLOAT || v == DXGI_FORMAT_R32G32B32A32_FLOAT; };
    if (!isColor(f.noisyColor.resource->GetDesc().Format) || !isColor(f.output.resource->GetDesc().Format) ||
        !isColor(f.normalRoughness.resource->GetDesc().Format) || !isColor(f.diffuseAlbedo.resource->GetDesc().Format) ||
        !isColor(f.specularAlbedo.resource->GetDesc().Format)) {
        error_ = "Prototype requires linear float RGBA color, reflectance and packed normals"; return false;
    }
    auto mv = f.motion.resource->GetDesc().Format;
    if(f.responsivityMask.resource){auto format=f.responsivityMask.resource->GetDesc().Format;if(format!=DXGI_FORMAT_R16_FLOAT&&format!=DXGI_FORMAT_R8_SNORM){error_="RR responsivity mask requires R16F or R8 SNORM";return false;}}
    if (mv != DXGI_FORMAT_R16G16_FLOAT && mv != DXGI_FORMAT_R32G32_FLOAT) {
        error_ = "RR motion must be RG16F or RG32F"; return false;
    }
    float wm = 0, pm = 0;
    for (float v : f.worldToView) { if (!std::isfinite(v)) { error_ = "Invalid camera matrix"; return false; } wm += std::abs(v); }
    for (float v : f.viewToClip) { if (!std::isfinite(v)) { error_ = "Invalid projection matrix"; return false; } pm += std::abs(v); }
    if (!wm || !pm || !std::isfinite(f.jitterX) || !std::isfinite(f.jitterY) ||
        !std::isfinite(f.motionScaleX) || !std::isfinite(f.motionScaleY) ||
        f.motionScaleX == 0 || f.motionScaleY == 0 || !std::isfinite(f.preExposure) || f.preExposure <= 0 ||
        !std::isfinite(f.exposureScale) || f.exposureScale <= 0 || !std::isfinite(f.deltaMs) || f.deltaMs <= 0) {
        error_ = "Missing camera, motion scale, exposure or frame time"; return false;
    }
    return true;
}
bool Backend::evaluate(ID3D12GraphicsCommandList* list, const Frame& f) {
    if (!feature_ || !list) { error_ = "RR feature not ready"; return false; }
    Microsoft::WRL::ComPtr<ID3D12Device> listOwner;
    if (FAILED(list->GetDevice(IID_PPV_ARGS(&listOwner))) || listOwner.Get() != device_.Get()) {
        error_ = "RR command list belongs to a different device"; return false;
    }
    if (!validate(f)) return false;
    std::vector<D3D12_RESOURCE_BARRIER> before, after;
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> seen;
    auto transition = [&](Texture t, D3D12_RESOURCE_STATES target) {
        if (!seen.emplace(t.resource, t.state).second || t.state == target) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {t.resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, t.state, target};
        before.push_back(b);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        after.push_back(b);
    };
    for (auto t : {f.noisyColor, f.depth, f.motion, f.diffuseAlbedo, f.specularAlbedo,
                  f.normalRoughness, f.specularHitDistance}) transition(t, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if(f.responsivityMask.resource)transition(f.responsivityMask,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    transition(f.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!before.empty()) list->ResourceBarrier(UINT(before.size()), before.data());
    NVSDK_NGX_D3D12_DLSSD_Eval_Params p{};
    p.pInColor = f.noisyColor.resource; p.pInOutput = f.output.resource;
    p.pInDepth = f.depth.resource; p.pInMotionVectors = f.motion.resource;
    p.pInDiffuseAlbedo = f.diffuseAlbedo.resource; p.pInSpecularAlbedo = f.specularAlbedo.resource;
    p.pInNormals = f.normalRoughness.resource; p.pInSpecularHitDistance = f.specularHitDistance.resource;
    p.pInResponsivityMask = f.responsivityMask.resource;
    p.pInWorldToViewMatrix = const_cast<float*>(f.worldToView.data());
    p.pInViewToClipMatrix = const_cast<float*>(f.viewToClip.data());
    p.InJitterOffsetX = f.jitterX; p.InJitterOffsetY = f.jitterY;
    p.InMVScaleX = f.motionScaleX; p.InMVScaleY = f.motionScaleY;
    p.InPreExposure = f.preExposure; p.InExposureScale = f.exposureScale;
    p.InFrameTimeDeltaInMsec = f.deltaMs; p.InReset = f.reset ? 1 : 0;
    p.InRenderSubrectDimensions = {iw_, ih_};
    bool ok = result(NGX_D3D12_EVALUATE_DLSSD_EXT(list, feature_, parameters_, &p), "RR evaluate");
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = f.output.resource;
    list->ResourceBarrier(1, &uav);
    if (!after.empty()) list->ResourceBarrier(UINT(after.size()), after.data());
    if (ok) ++evaluations_;
    return ok;
}
void Backend::shutdownAfterGpuIdle() {
    if (feature_) { NVSDK_NGX_D3D12_ReleaseFeature(feature_); feature_ = nullptr; }
    if (parameters_) { NVSDK_NGX_D3D12_DestroyParameters(parameters_); parameters_ = nullptr; }
    if (initialized_) { NVSDK_NGX_D3D12_Shutdown1(device_.Get()); initialized_ = false; }
    device_.Reset();
}
}
