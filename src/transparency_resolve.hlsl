// Same-frame transparent path composition. No native history/denoised colour.
Texture2D<float4> rawColour : register(t0);
Texture2D<float4> normalGuide : register(t1);
Texture2D<float4> diffuseGuide : register(t2);
Texture2D<float4> specularGuide : register(t3);
Texture2D<float> rayLength : register(t4);
Texture2D<float> hitDistance : register(t5);
Texture2D<float4> viewDirection : register(t6);
Texture2D<float4> transmission : register(t7);
Texture2D<float> savedDomain : register(t8);
RWTexture2D<float4> colourOut : register(u0);
RWTexture2D<float4> normalOut : register(u1);
RWTexture2D<float4> diffuseOut : register(u2);
RWTexture2D<float4> specularOut : register(u3);
RWTexture2D<float> depthOut : register(u4);
RWTexture2D<float> hitOut : register(u5);
cbuffer Frame : register(b0) {
 uint width,height,fieldWidth,frame;
 uint renderMethod,extendOddEdge,pad0,pad1;
 row_major float4x4 worldToView;
};
float3 transparentSample(uint2 p) {
 float4 value=rawColour.Load(int3(p,0));
 float depth=rayLength.Load(int3(p,0));
 uint bankLo=p.x<fieldWidth?0u:fieldWidth;
 uint bankHi=bankLo+fieldWidth-1u;
 if(value.a==0||!all(isfinite(value))||!isfinite(depth)||depth<=0||depth>=65504||
    p.x<=bankLo||p.x>=bankHi||p.y==0||p.y+1>=height)return value.rgb;
 float peak=max(value.r,max(value.g,value.b));
 // Cheap rejection for ordinary samples. This cannot make the filter more
 // aggressive than the full surface-coherent rank test below.
 float lower=1e30;
 const int2 offsets[8]={int2(-1,0),int2(1,0),int2(0,-1),int2(0,1),
  int2(-1,-1),int2(1,-1),int2(-1,1),int2(1,1)};
 [unroll]for(uint i=0;i<4;i++){
  float3 c=rawColour.Load(int3(int2(p)+offsets[i],0)).rgb;
  lower=min(lower,max(c.r,max(c.g,c.b)));
 }
 if(peak<=6*max(lower,1e-6))return value.rgb;
 float4 normal=normalGuide.Load(int3(p,0));
 float3 albedo=diffuseGuide.Load(int3(p,0)).rgb;
 float largest=-1,second=-1;float3 sum=0,largestColour=0;uint count=0;
 [unroll]for(uint j=0;j<8;j++){
  int2 q=int2(p)+offsets[j];float4 c=rawColour.Load(int3(q,0));
  float z=rayLength.Load(int3(q,0));float4 n=normalGuide.Load(int3(q,0));
  float3 d=diffuseGuide.Load(int3(q,0)).rgb;
  if(c.a!=value.a||!all(isfinite(c))||!isfinite(z)||z<=0||z>=65504||
     abs(z-depth)>.1*depth||!all(isfinite(n))||!all(isfinite(d))||
     dot(n.xyz,normal.xyz)<.75||abs(n.a-normal.a)>.25||any(abs(d-albedo)>.2))continue;
  float v=max(c.r,max(c.g,c.b));sum+=c.rgb;count++;
  if(v>largest){second=largest;largest=v;largestColour=c.rgb;}
  else second=max(second,v);
 }
 // One noisy neighbour must not veto rejection of another isolated impulse.
 // Clusters retain a high second-ranked sample and remain untouched.
 if(count<5||second<=1e-6||peak<=6*second)return value.rgb;
 return (sum-largestColour)/(count-1);
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID) {
 if(id.x>=width||id.y>=height||renderMethod!=0||fieldWidth==0)return;
 uint2 p=id.xy,q=p;
 if(extendOddEdge!=0&&width>1)q.x=min(q.x,(width&~1u)-1u);
 uint parity=(frame&1u)^1u;
 q.x=(q.x>>1u)+(((q.x^q.y)&1u)==parity?0u:fieldWidth);
 uint2 other=uint2(q.x<fieldWidth?q.x+fieldWidth:q.x-fieldWidth,q.y);
 uint aw,ah;rawColour.GetDimensions(aw,ah);
 if(q.x>=aw||other.x>=aw||q.y>=ah)return;
 float4 current=rawColour.Load(int3(q,0));
 // The native interleave leaves ordinary opaque colour (alpha=0) untouched.
 if(current.a==0||!all(isfinite(current)))return;
 float a=transmission.Load(int3(q,0)).a;
 float b=transmission.Load(int3(other,0)).a;
 // Native negative-alpha convention identifies the complementary field.
 float weight=a<0?1-b:a;
 // Endpoints are valid native composition, not an opaque-pixel sentinel.
 // Distant clouds reach 1 in the sky field and 0 in its complement; skipping
 // them leaves alternating cloud/sky guides at the partial-transparency edge.
 if(!isfinite(weight)||weight<0||weight>1)return;
 uint bankLo=other.x<fieldWidth?0u:fieldWidth;
 uint bankHi=min(bankLo+fieldWidth,aw)-1u;
 int2 o=int2(other);
 // Native interleave reconstructs the missing checker sample from vertical
 // neighbours, the paired field sample and one parity-directed horizontal tap.
 // A symmetric left/right gather has a different sample centre.
 int dx=int((p.x<<1u)&2u)-1;
 float3 opposite=0.25*(transparentSample(uint2(o.x,max(o.y-1,0)))+
  transparentSample(uint2(o.x,min(o.y+1,int(height)-1)))+
  transparentSample(uint2(o))+
  transparentSample(uint2(clamp(o.x+dx,int(bankLo),int(bankHi)),o.y)));
 float domain=savedDomain.Load(int3(0,0,0));
 if(!all(isfinite(opposite))||!isfinite(domain)||domain<=0)return;
 // Choose one physical surface for all guides, rather than averaging depths
 // or normals belonging to different layers. The tie is field-order stable.
 uint2 guide=weight>0.5?other:weight<0.5?q:(q.x<other.x?q:other);
 // Equal-weight finite geometry can include a smooth front pane and the
 // detailed transmitted surface. Resolve that ambiguity by depth instead of
 // physical bank order. Sky/geometry ties retain the existing stable choice.
 if(weight==0.5){
  float front=rayLength.Load(int3(q,0));
  float back=rayLength.Load(int3(other,0));
  if(isfinite(front)&&isfinite(back)&&front>0&&back>0&&front<65504&&back<65504&&front!=back)
   guide=front>back?q:other;
 }
 float distance=rayLength.Load(int3(guide,0));
 float3 direction=viewDirection.Load(int3(guide,0)).xyz;
 float4 n=normalGuide.Load(int3(guide,0));
 float4 d=diffuseGuide.Load(int3(guide,0));
 float4 s=specularGuide.Load(int3(guide,0));
 float hit=hitDistance.Load(int3(guide,0));
 if(!all(isfinite(float4(direction,distance)))||!all(isfinite(n))||!all(isfinite(d))||!all(isfinite(s))||!isfinite(hit))return;
 colourOut[p]=float4(max(lerp(transparentSample(q),opposite,weight)/domain,0),1);
 normalOut[p]=n;diffuseOut[p]=d;specularOut[p]=s;
 float cameraZ=abs(mul(float4(direction,0),worldToView).z);
 depthOut[p]=distance>=65504?1e6:max(distance*cameraZ,1e-4);
 hitOut[p]=max(hit,0);
}
