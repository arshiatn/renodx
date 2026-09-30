// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 26 13:13:42 2026

// WARHAMMER 3 lensflare03 - mirrored flare sprite (lense_flare_3).
// Added onto the frame after t00, so it needs t00's Paper White too.
// SI.lensflare is HDR only.

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
  float4x4 colour_matrix : packoffset(c2);
}

SamplerState lense_flare_sampler_s : register(s0);
Texture2D<float4> lense_flare_3_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Shared by all four lens flare passes. Stock colour in, final colour out.
float4 ApplyWH3LensFlare(float4 stock_color)
{
  // UNORM clamp the FP16 target lost.
  float4 flare = saturate(stock_color);

  // Frame is 2.2-encoded here. Scale in linear, same ratio t00 uses.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= ((HDR >= 0.5f) ? SI.lensflare : 1.f)
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
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v1.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, colour_matrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, colour_matrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, colour_matrix._m02_m12_m22_m32);
  r0.xyz = sun_flare_ps * r1.xyz;
  r1.xy = half_texel_offset.yx + v2.yx;
  r1.xy = float2(-0.5,-0.5) + r1.xy;
  r1.xy = r1.xy * float2(-1,1) + float2(0.5,0.5);
  r1.xyzw = lense_flare_3_texture.Sample(lense_flare_sampler_s, r1.xy).xyzw;
  r0.w = v1.w;
  o0.xyzw = ApplyWH3LensFlare(r1.xyzw * r0.xyzw);
  return;
}