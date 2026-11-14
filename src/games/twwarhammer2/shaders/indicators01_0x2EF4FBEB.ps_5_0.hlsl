// gridspline.hlsl ps40_main_fire_arc (the missile units' fire arcs; ATN's 2026-11-14 session: 9 traces): the game's, its colour x Unit/Building Indicator Brightness in HDR
// (SI.someindicators, 1 the game's; Attila's some_indicators).
// Decompiled from the game's 2EF4FBEB by 3Dmigoto v1.3.16; WARP tools/tw/warp_tests/par_w2/run_indicators.sh.

cbuffer grid_spline_PS : register(b0)
{
  float4 g_spline_colour : packoffset(c0);
  float3 g_mouse_intersected_position : packoffset(c1);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.w = 1;
  r1.xyzw = t_diffuse.Sample(s_diffuse_s, v1.xy).xyzw;
  r0.xyz = r1.xyz;
  r0.xyzw = g_spline_colour.xyzw * r0.xyzw;
  r1.x = 2;
  o0.xyzw = r0.xyzw * r1.xxxw;
  if (SI.hdr >= 1.f) o0.xyz *= SI.someindicators;  // Unit/Building Indicator Brightness; SDR stays the game's
  return;
}
