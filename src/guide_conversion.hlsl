// Bedrock 1.26.5203.0 material layout, verified against the captured final
// combine shader. This converts material guides only; it does not assemble
// noisy light. Specular reflectance uses NVIDIA's recommended environment BRDF
// approximation with the captured primary view direction and linear roughness.
Texture2D<float2> packedNormal : register(t0);
Texture2D<float4> colourMetallic : register(t1);
Texture2D<float4> emissiveRoughness : register(t2);
Texture2D<float4> primaryViewDirection : register(t3);
RWTexture2D<float4> normalRoughness : register(u0);
RWTexture2D<float4> diffuseAlbedo : register(u1);
RWTexture2D<float4> specularAlbedo : register(u2);
cbuffer Dimensions : register(b0) { uint width; uint height; };

float3 decodeNormal(float2 e) {
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(e.yx)) * float2(e.x >= 0 ? 1 : -1, e.y >= 0 ? 1 : -1);
    }
    return normalize(n);
}

// Expanded scalar form of the rational environment-BRDF fit documented in
// NVIDIA's Streamline DLSS RR programming guide, section 4.2.1.
float3 specularGuide(float3 f0, float roughness, float noV) {
    float a = roughness * roughness;
    float a3 = a * a * a;
    float v = abs(noV), v2 = v * v, v3 = v2 * v;
    float biasNumerator = 0.99044 - 1.28514*v + a*(1.29678 - 0.755907*v);
    float biasDenominator = 1 + 2.92338*v + 59.4188*v3
        + a*(20.3225 - 27.0302*v + 222.592*v3)
        + a3*(121.563 + 626.13*v + 316.627*v3);
    float scaleNumerator = 0.0365463 + 3.32707*v + a*(9.0632 - 9.04756*v);
    float scaleDenominator = 1 + 3.59685*v2 - 1.36772*v3
        + a*(9.04401 - 16.3174*v2 + 9.22949*v3)
        + a3*(5.56589 + 19.7886*v2 - 20.2123*v3);
    float bias = (biasNumerator/biasDenominator) * saturate(f0.g * 50);
    float scale = scaleNumerator/scaleDenominator;
    return f0 * max(scale,0) + max(bias,0);
}

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    uint2 p = id.xy;
    float4 material = colourMetallic.Load(int3(p,0));
    float3 linearColour = material.rgb * material.rgb;
    float metal = material.a;
    float roughness = emissiveRoughness.Load(int3(p,0)).a;
    float3 n = decodeNormal(packedNormal.Load(int3(p,0)));
    float3 direction = primaryViewDirection.Load(int3(p,0)).xyz;
    float lengthSquared = dot(direction,direction);
    // Sky directions can be unset. Use the normal for a finite neutral guide.
    float3 view = lengthSquared > 1e-20 ? direction * rsqrt(lengthSquared) : n;
    float3 f0 = lerp(float3(0.04,0.04,0.04), linearColour, metal);
    normalRoughness[p] = float4(n, roughness);
    diffuseAlbedo[p] = float4(linearColour * (1.0 - metal), 1.0);
    specularAlbedo[p] = float4(specularGuide(f0,roughness,dot(n,-view)),1.0);
}
