#pragma once
#include <d3d12.h>
#include <wrl/client.h>
namespace bedrock_rr {
// ReShade 6.8's public QueryInterface convention for retrieving the original
// COM object. Unsupported queries retain the standard COM identity path.
inline Microsoft::WRL::ComPtr<IUnknown> deviceIdentity(ID3D12Device* device) {
    constexpr GUID unwrapped={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    Microsoft::WRL::ComPtr<IUnknown> native,identity;
    if(!device)return identity;
    if(SUCCEEDED(device->QueryInterface(unwrapped,reinterpret_cast<void**>(native.GetAddressOf())))&&native)
        native->QueryInterface(IID_PPV_ARGS(&identity));
    else device->QueryInterface(IID_PPV_ARGS(&identity));
    return identity;
}
inline bool sameDevice(ID3D12Device* a,ID3D12Device* b) {
    auto left=deviceIdentity(a),right=deviceIdentity(b);
    return left&&right&&left.Get()==right.Get();
}
}
