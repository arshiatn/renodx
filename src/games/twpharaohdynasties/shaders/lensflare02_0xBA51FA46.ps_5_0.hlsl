// PHARAOH lensflare02 - sprite 2, straight blit of lense_flare_2_texture.
//
// Draws after t01, so it never gets t01's paper-white multiply and would otherwise
// sit at UI brightness. SI.lensflare is the user multiplier.
// Not god rays - those are composited inside t01 and already carry SI.godrays.

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
Texture2D<float4> lense_flare_2_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../lensflare.hlsli"


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
  r0.xyzw = lense_flare_2_texture.Sample(lense_flare_sampler_s, r0.xy).xyzw;
  o0.xyzw = ApplyPharaohLensOverlay(v1.xyzw * r0.xyzw, SI.lensflare);
  return;
}