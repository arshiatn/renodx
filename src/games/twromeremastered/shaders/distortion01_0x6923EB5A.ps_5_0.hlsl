// ---- Created with 3Dmigoto v1.3.16 (RenoDX dump of FeralShaders, 2026-10-20)

// HeatHaze.hlsl PS_ApplyDistortion (offsets only): distortion00's slider.

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);




// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = t2.Sample(s0_s, v1.xy).xy;
  r0.xy *= (HDR >= 0.5f) ? SI.uvdistort : 1.f;  // distortion slider, HDR only
  r0.xy = v1.xy + r0.xy;
  o0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  return;
}