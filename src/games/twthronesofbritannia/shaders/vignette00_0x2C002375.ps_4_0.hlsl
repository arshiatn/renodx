// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 19 15:33:24 2026

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
}



// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = half_texel_offset.xy + v1.xy;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x * 2 + -0.899999976);
  r0.x = log2(r0.x);
  r0.x = 1.70000005 * r0.x;
  r0.x = exp2(r0.x);
  r0.y = -0.00392156886 + r0.x;
  o0.w = r0.x;
  if (HDR == 1.f) {
        o0.w = saturate(o0.w * SI.vignette);  // adding vignette for slider; >1 makes (1-a) negative
  }
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;
  o0.xyz = float3(0,0,0);
  return;
}