// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 30 00:15:21 2026

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> Base : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float4 v2 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Base.Sample(LinearClampSampler_s, v1.xy).xyzw;
  r1.xyzw = Base.Sample(LinearClampSampler_s, v2.xy).xyzw;
  r0.xyzw = max(r1.xyzw, r0.xyzw);
  r1.xyzw = Base.Sample(LinearClampSampler_s, v2.zw).xyzw;
  o0.xyzw = max(r1.xyzw, r0.xyzw);
  return;
}