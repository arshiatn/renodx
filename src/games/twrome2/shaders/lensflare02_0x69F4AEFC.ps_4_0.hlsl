// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 23 23:04:00 2026

// ROME 2 lensflare02 - sprite 2, straight blit. Draws after t0x.

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
}

SamplerState lense_flare_2_sampler_s : register(s0);
Texture2D<float4> lense_flare_2_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Shared by all four lens flare passes.
float4 ApplyRome2LensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // The game's own add, scaled in linear with the frame's gamma (the swap chain proxy's decode): at Lens Flare 1 and
  // Paper White = UI it is the game's.
  flare.xyz = EncodeFrame(DecodeFrame(flare.xyz) * (SI.lensflare * (SI.diffuse_white_nits / SI.graphics_white_nits)));

  return flare;
}


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = half_texel_offset.xy + v2.xy;
  r0.xyzw = lense_flare_2_sampler.Sample(lense_flare_2_sampler_s, r0.xy).xyzw;
  o0.xyzw = ApplyRome2LensFlare(v1.xyzw * r0.xyzw);
  return;
}