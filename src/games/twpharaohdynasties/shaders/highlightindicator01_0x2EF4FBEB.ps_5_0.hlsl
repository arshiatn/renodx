// PHARAOH highlightindicator01 - grid / spline overlay (g_spline_colour * diffuse).
//
// Stock ends with a hard 2x on the RGB, the same shape as highlightindicator00's 10x
// and with nothing clamping it once the target is FP16.
//
// The clamp applies in SDR and HDR alike; SI.someindicators then sets the level in HDR
// only, so this tracks the other indicators. 1.0 is exactly stock.

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

  // Stock is o0.xyzw = r0.xyzw * r1.xxxw, i.e. 2x on RGB and the texture alpha on w.
  // saturate() restores the write clamp in BOTH modes - the target is FP16 in SDR too,
  // so without it stock's overbright reads thousands of nits there as well. The slider
  // stays HDR-only.
  o0.xyz = saturate(r0.xyz * r1.xxx);

  o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
  if (HDR >= 0.5f) o0.xyz *= SI.someindicators;
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);

  o0.w = saturate(r0.w * r1.w);
  return;
}
