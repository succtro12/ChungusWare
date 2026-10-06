#ifdef BRIDGE_HANDOFF
Texture2D<float4> sourceColour : register(t0);
Texture2D<float> savedDomain : register(t1);
RWTexture2D<float4> nativeColour : register(u0);
cbuffer Dimensions : register(b0) { uint width,height; };
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=width||id.y>=height)return;
    float4 c=sourceColour.Load(int3(id.xy,0));
    nativeColour[id.xy]=float4(c.rgb*savedDomain.Load(int3(0,0,0)),c.a);
}
#else
Texture2D<float4> rawColour : register(t0);
Texture2D<float4> packedNormals : register(t1);
Texture2D<float4> diffuseGuide : register(t2);
Texture2D<float4> specularGuide : register(t3);
Texture2D<float> rayLength : register(t4);
Texture2D<float2> motion : register(t5);
Texture2D<float> hitDistance : register(t6);
StructuredBuffer<float4> incidentLight : register(t7);
Texture2D<float4> viewDirection : register(t8);
RWTexture2D<float4> colourOut : register(u0);
RWTexture2D<float4> normalsOut : register(u1);
RWTexture2D<float4> diffuseOut : register(u2);
RWTexture2D<float4> specularOut : register(u3);
RWTexture2D<float> depthOut : register(u4);
RWTexture2D<float2> motionOut : register(u5);
RWTexture2D<float> hitOut : register(u6);
RWTexture2D<float> domainOut : register(u7);
cbuffer Frame : register(b0) {
    uint width,height,fieldWidth,frame;
    uint renderMethod; float colourScale; uint sceneDomain; uint extendOddEdge;
    row_major float4x4 worldToView;
};
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
    if(id.x>=width||id.y>=height)return;
    uint2 p=id.xy,q=p;
    // Retained odd-width fix: this validated combine leaves its final column unwritten.
    if(extendOddEdge!=0&&renderMethod!=2&&width>1)q.x=min(q.x,(width&~1u)-1u);
    // Exact source-column selection from Bedrock d42323fa90bfb5ce.
    if(renderMethod!=2) {
        uint leftParity=((renderMethod==0&&(frame&1)!=0)?1:0)^1;
        q.x=(q.x>>1)+(((q.x^q.y)&1)==leftParity?0:fieldWidth);
    }
    // Preserve Bedrock's native SR-boundary HDR domain. With TAA disabled,
    // a2c049a5a3f59ee4 already applies exposure at equal render/display size.
    // At reduced resolution it quarters radiance instead; 2c42e7d773939fed,
    // which still runs after our SR bypass, restores 4 * exposure.y/exposure.x.
    // Applying that adjustment here duplicates it in either path. RR input
    // and output remain in the same linear HDR domain as the native SR call.
    float4 exposure=incidentLight[0];
    float ratio=(exposure.x>1e-12&&exposure.y>1e-12&&all(isfinite(exposure.xy)))?exposure.y/exposure.x:1;
    ratio=isfinite(ratio)&&ratio>1e-12?ratio:1;
    // RR does not consume SR's exposure parameters. The optional scene-domain
    // path removes the known native pre-exposure, then restores that exact
    // scale after RR using a same-frame GPU snapshot, without changing guides.
    // Domain 2 restores the exact pre-exposure-fix RR input range. Its
    // reciprocal is applied after RR, so exposure is not applied twice.
    float toScene=sceneDomain==2?colourScale*ratio:sceneDomain==1?(colourScale==4?4:1/ratio):1;
    if(all(p==0))domainOut[uint2(0,0)]=1/toScene;
    colourOut[p]=float4(max(rawColour.Load(int3(q,0)).rgb*toScene,0),1);
    normalsOut[p]=packedNormals.Load(int3(q,0));
    diffuseOut[p]=diffuseGuide.Load(int3(q,0));
    specularOut[p]=specularGuide.Load(int3(q,0));
    float3 direction=viewDirection.Load(int3(q,0)).xyz;
    float distance=rayLength.Load(int3(q,0));
    float cameraZ=abs(mul(float4(direction,0),worldToView).z);
    depthOut[p]=distance>=65504?1e6:max(distance*cameraZ,1e-4);
    motionOut[p]=motion.Load(int3(q,0));
    hitOut[p]=max(hitDistance.Load(int3(q,0)),0);
}
#endif
