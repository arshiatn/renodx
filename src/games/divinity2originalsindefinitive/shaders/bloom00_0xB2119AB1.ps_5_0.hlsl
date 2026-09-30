// ---- Created with 3Dmigoto v1.3.16 on Thu Sep 10 01:08:24 2026

cbuffer _Globals : register(b0)
{
  float Amount : packoffset(c0);
}

cbuffer PerView : register(b12)
{
  row_major float4x4 global_View : packoffset(c0);
  row_major float4x4 global_Projection : packoffset(c4);
  row_major float4x4 global_ViewProjection : packoffset(c8);
  float4 global_ViewPos : packoffset(c12);
  float4 global_ViewInfo : packoffset(c13);
  float4 global_ScaleAndBias : packoffset(c14);
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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Base2.SampleLevel(LinearClampSampler_s, v1.xy, 0).xyzw;
  r1.xy = float2(2,2) / global_ViewInfo.zw;
  r1.zw = -r1.xy;
  r2.xyzw = v1.xyxy + r1.zyxw;
  r3.xyzw = Base2.SampleLevel(LinearClampSampler_s, r2.xy, 0).xyzw;
  r2.xyzw = Base2.SampleLevel(LinearClampSampler_s, r2.zw, 0).xyzw;
  r3.xyzw = float4(0.0625,0.0625,0.0625,0.0625) * r3.xyzw;
  r0.xyzw = r0.xyzw * float4(0.25,0.25,0.25,0.25) + r3.xyzw;
  r3.xyz = -r1.xyx;
  r1.zw = v1.xy + r3.xy;
  r4.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.xyzw = r4.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r0.xyzw;
  r0.xyzw = r2.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r0.xyzw;
  r1.zw = v1.xy + r1.xy;
  r2.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.xyzw = r2.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r0.xyzw;
  r3.w = 0;
  r1.zw = v1.xy + r3.zw;
  r2.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.xyzw = r2.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r2.y = -r1.y;
  r2.xw = float2(0,0);
  r1.zw = v1.xy + r2.xy;
  r3.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.xyzw = r3.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r2.z = r1.x;
  r1.zw = v1.xy + r2.zw;
  r2.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.zw, 0).xyzw;
  r0.xyzw = r2.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r1.x = 0;
  r1.xy = v1.xy + r1.xy;
  r1.xyzw = Base2.SampleLevel(LinearClampSampler_s, r1.xy, 0).xyzw;
  r0.xyzw = r1.xyzw * float4(0.125,0.125,0.125,0.125) + r0.xyzw;
  r1.xyzw = Base.SampleLevel(LinearClampSampler_s, v1.xy, 0).xyzw;

  //add bloom slider
  o0.xyzw = (r0.xyzw * Amount * SI.bloom) + r1.xyzw;
  return;
}