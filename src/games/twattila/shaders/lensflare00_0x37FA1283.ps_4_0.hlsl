// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 25 11:31:54 2026

cbuffer lense_flare_PS : register(b0)
{
  float time : packoffset(c0);
  float2 half_texel_offset : packoffset(c0.y);
  float vignette_strength : packoffset(c0.w);
  float sun_flare_ps : packoffset(c1);
}

SamplerState lense_flare_1_sampler_s : register(s0);
Texture2D<float4> lense_flare_1_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


float4 ApplyAttilaLensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // Made for a 2.2 screen (ignores in-game gamma); the frame is 2.0. Scale in linear.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= (HDR == 1.f ? SI.lensflare : 1.f)
                * (SI.diffuse_white_nits / SI.graphics_white_nits);
  flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear, 2.0f);

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
  r0.xyzw = lense_flare_1_sampler.Sample(lense_flare_1_sampler_s, r0.xy).xyzw;
  o0.xyzw = ApplyAttilaLensFlare(v1.xyzw * r0.xyzw);
  return;
}