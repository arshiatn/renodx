// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 29 11:17:51 2026

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

SamplerState LinearSampler_s : register(s0);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = Base2.SampleLevel(LinearSampler_s, v1.xy, 0).xz;
  r1.y = Base2.SampleLevel(LinearSampler_s, v3.zw, 0).y;
  r1.w = Base2.SampleLevel(LinearSampler_s, v3.xy, 0).w;
  r1.xz = r0.xy;
  r0.z = dot(r1.xyzw, float4(1,1,1,1));
  r0.z = cmp(r0.z < 9.99999975e-006);
  if (r0.z != 0) {
    o0.xyzw = Base.SampleLevel(LinearSampler_s, v1.xy, 0).xyzw;
  } else {
    r0.zw = cmp(r1.zx < r1.wy);
    r0.xw = r0.zw ? r1.wy : -r0.yx;
    r1.x = cmp(abs(r0.w) < abs(r0.x));
    r0.yz = float2(0,0);
    r0.xy = r1.xx ? r0.xy : r0.zw;
    r0.zw = float2(1,1) / global_ViewInfo.zw;
    r0.xy = r0.xy * r0.zw + v1.xy;
    o0.xyzw = Base.SampleLevel(LinearSampler_s, r0.xy, 0).xyzw;
  }
  return;
}