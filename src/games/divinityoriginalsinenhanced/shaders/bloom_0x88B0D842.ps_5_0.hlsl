// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 23 12:39:58 2026

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
}

SamplerState LinearClampSampler_s : register(s0);
Texture2D<float4> Base : register(t0);
Texture2D<float4> Base2 : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(1,1) / global_ViewInfo.zw;
  r0.zw = -r0.xy;
  r1.xyzw = v1.xyxy + r0.zyxw;
  r2.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.xy, 0).xyzw;
  r1.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.zw = v1.xy + -r0.xy;
  r0.xy = v1.xy + r0.xy;
  r3.xyzw = Base2.SampleLevel(LinearClampSampler_s, r0.xy, 0).xyzw;
  r0.xyzw = Base2.SampleLevel(LinearClampSampler_s, r0.zw, 0).xyzw;
  r0.xyzw = r2.xyzw + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.xyzw;
  r0.xyzw = r0.xyzw + r3.xyzw;
  r1.xy = (int2)v0.xy;
  r1.zw = float2(0,0);
  r1.xyzw = Base.Load(r1.xyz).xyzw;

  // Stock adds the 4-tap average at full strength (asm 24: mad o0, r0, 0.25, r1).
  // SI.bloom: 1 = stock, HDR modes only. SDR stays stock.
  float4 custom_bloor = r0.xyzw * float4(0.25, 0.25, 0.25, 0.25);
  custom_bloor *= (HDR == 1.f ? SI.bloom : 1.f);
  o0.xyzw = custom_bloor + r1.xyzw;
  return;
}