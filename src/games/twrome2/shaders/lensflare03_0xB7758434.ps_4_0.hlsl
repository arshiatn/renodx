// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 23 23:04:00 2026

// ROME 2 lensflare03 - sprite 3, x-mirrored. Draws after t0x.

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
}

SamplerState lense_flare_3_sampler_s : register(s0);
Texture2D<float4> lense_flare_3_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Shared by all four lens flare passes.
float4 ApplyRome2LensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // Frame is gamma-encoded here; scale in linear by t0x's scene ratio.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= (HDR == 1.f ? SI.lensflare : 1.f)
                * (SI.diffuse_white_nits / SI.graphics_white_nits);
  flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear);

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

  r0.xy = half_texel_offset.yx + v2.yx;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = r0.xy * float2(-1,1) + float2(0.5,0.5);
  r0.xyzw = lense_flare_3_sampler.Sample(lense_flare_3_sampler_s, r0.xy).xyzw;
  o0.xyzw = ApplyRome2LensFlare(v1.xyzw * r0.xyzw);
  return;
}