// indicator.hlsl ps30_main (the units' and buildings' indicators drawn without the G-buffer test): the game's, its colour x
// Unit/Building Indicator Brightness in HDR (SI.indicator_brightness, 1 the game's; Attila's some_indicators). Decompiled
// from the game's D12FA660 by 3Dmigoto v1.3.16; WARP test/tw_shogun2_shaders part 15.
#include "../shared.h"

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
  const float4 indicator = indicator_texture_sampler.Sample(indicator_texture_sampler_s, v1.xy);
  if (indicator.w - 0.00392156886f < 0.f) discard;
  o0 = indicator;
  if (HDR >= 1.f) o0.xyz *= SI.indicator_brightness;
}
