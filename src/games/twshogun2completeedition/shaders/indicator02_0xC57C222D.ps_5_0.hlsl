// indicator.hlsl ps30_main_to_overlay (selection rings and firing arcs on the sea's colour overlay; sea_overlay keeps its
// draws full size): the game's, its colour x Unit/Building Indicator Brightness in HDR (SI.indicator_brightness, 1 the
// game's; Attila's some_indicators). Decompiled from the game's C57C222D by 3Dmigoto v1.3.16; WARP test/tw_shogun2_shaders
// part 15.
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
}

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> indicator_texture_sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  // Fainter near the camera's height: (height above it)^2 / 2500, 0.3 to 1.
  const float height = camera_position.y - v2.y;
  const float fade = max(0.300000012f, min(1.f, 0.00039999999f * (height * height)));
  const float4 indicator = v3 * indicator_texture_sampler.Sample(indicator_texture_sampler_s, v1.xy);
  if (indicator.w * fade - 0.00392156886f < 0.f) discard;
  o0.xyz = indicator.xyz;
  o0.w = indicator.w * fade;
  if (HDR >= 1.f) o0.xyz *= SI.indicator_brightness;
}
