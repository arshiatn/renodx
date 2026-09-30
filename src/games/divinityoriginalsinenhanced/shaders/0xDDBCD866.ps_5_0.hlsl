// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 29 11:18:16 2026

cbuffer _Globals : register(b0)
{
  float3 VignetteColor : packoffset(c0);
  float2 VignetteParams : packoffset(c1);
}

Texture2D<float4> Base : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  float2 w1 : TexCoord1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(-0.5,-0.5) + w1.xy;
  r0.xy = r0.xy + r0.xy;
  r0.xy = log2(abs(r0.xy));
  r0.xy = VignetteParams.yy * r0.xy;
  r0.xy = exp2(r0.xy);
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = saturate(VignetteParams.x * r0.x);

  r0.x = saturate(r0.x * (HDR == 1.f ? SI.vignette : 1.f));

  r1.xy = (int2)v0.xy;
  r1.zw = float2(0, 0);
  r0.yzw = Base.Load(int3(r1.xyz)).xyz;
  r1.xyz = VignetteColor.xyz + -r0.yzw;
  o0.xyz = r0.xxx * r1.xyz + r0.yzw;
  o0.w = 1;
  return;
}