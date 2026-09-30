// TROY lensflare01 - sprite 1, rotating clockwise (+3 deg/sec).
// 0x65C7FD8D is its counter-clockwise partner, same texture.
//
// Draws after t01 (467-471) so it never gets t01's paper-white multiply and would sit
// at UI brightness; ApplyTroyLensFlare applies the same ratio, in linear because the
// frame is gamma-encoded by then. Not god rays - those are composited inside t01.

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float vignette_falloff : packoffset(c1);
  float sun_flare_ps : packoffset(c1.y);
  float4 photomode_crop : packoffset(c2);
}

SamplerState lense_flare_sampler_s : register(s0);
Texture2D<float4> lense_flare_1_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Shared by all five lens flare passes. Stock colour in, final colour out.
float4 ApplyTroyLensFlare(float4 stock_color)
{
  // Restores the write clamp the upgraded FP16 target no longer provides.
  float4 flare = saturate(stock_color);

  if (HDR >= 0.5f)
  {
    // Scale in linear, by the same ratio t01 applies to the scene.
    float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
    flare_linear *= SI.lensflare * (SI.diffuse_white_nits / SI.graphics_white_nits);
    flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear);
  }

  return flare;
}


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = half_texel_offset.yx + v2.yx;
  r0.xy = float2(-0.5,-0.5) + r0.xy;
  r0.xy = r0.xy + r0.xy;
  r0.z = 0.052359879 * time;
  sincos(r0.z, r1.x, r2.x);
  r0.zw = r2.xx * r0.xy;
  r2.x = r0.y * r1.x + -r0.z;
  r2.y = r0.x * r1.x + r0.w;
  r0.xy = r2.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r0.xyzw = lense_flare_1_texture.Sample(lense_flare_sampler_s, r0.xy).xyzw;
  o0.xyzw = ApplyTroyLensFlare(v1.xyzw * r0.xyzw);
  return;
}