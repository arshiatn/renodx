// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 30 00:15:17 2026

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);
Texture2D<float4> Base3 : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.z = Base3.Load(r0.xyz).w;
  r0.xyw = Base.Load(r0.xyw).xyz;
  r0.z = ceil(r0.z);
  r0.z = 1 + -r0.z;
  r1.xyzw = Base2.Sample(LinearClampSampler_s, v1.xy).xyzw;
  r0.z = r1.w * r0.z;
  r1.xyz = r1.xyz + -r0.xyw;
  o0.xyz = r0.zzz * r1.xyz + r0.xyw;
  o0.w = 1;
  return;
}