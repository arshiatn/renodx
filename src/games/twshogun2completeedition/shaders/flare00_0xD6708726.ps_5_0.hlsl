// lenseflare.hlsl ps30_flare_0: lense_flare_1, turning at 3 degrees a second. flare.hlsli.
#include "./flare.hlsli"

SamplerState lense_flare_1_sampler_s : register(s0);
Texture2D<float4> lense_flare_1_sampler : register(t0);

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0 = Flare(lense_flare_1_sampler, lense_flare_1_sampler_s, v1, v2, 3.0f);
}
