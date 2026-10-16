// hdr_lenseflare.hlsl ps30_main_lense_flare: the bloom target x g_bloom_amount plus lens flare ghosts from it (7
// scaled, mirrored copies, tinted); the tonemap adds it to the scene. HDR: the bloom x Bloom, the ghosts x Lens
// Flares (v24: Bloom scaled the whole target in the tonemap, ghosts too).
#include "../shared.h"

cbuffer lense_flare_PS : register(b0)
{
  float2 texel_size : packoffset(c0);
  float g_bloom_amount : packoffset(c0.z);
  float g_lens_flare_trail_amount : packoffset(c0.w);
}

SamplerState input_sampler_s : register(s0);
Texture2D<float4> input_sampler : register(t0);

// The game's tex2Dlod(sampler, texcoord.xyyy): its mip is texcoord.y. Kept.
float3 SampleBloom(float2 texcoord)
{
  return input_sampler.SampleLevel(input_sampler_s, texcoord, texcoord.y).rgb;
}

float3 FlareFalloff(float2 tc, float scale)
{
  float2 texcoord = 0.5f + (tc - 0.5f) * scale * 0.6f;
  float d = 1.f - saturate(distance(texcoord, 0.5f) * 2.f);
  float3 res = d * SampleBloom(texcoord);
  return res * saturate(log(dot(res, float3(1.f, 1.f, 1.f)) * 0.8f));
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 frame_tex = v2 + texel_size;
  float3 colour = g_bloom_amount * SampleBloom(frame_tex) * ((HDR >= 1.f) ? SI.bloom : 1.f);

  float3 flare = 0.f;
  flare += FlareFalloff(frame_tex, 6.0f) * float3(0.2f, 0.02f, 1.0f) * 5.0f;
  flare += FlareFalloff(frame_tex, 5.0f) * float3(0.0f, 0.1f, 0.8f) * 2.0f;
  flare += FlareFalloff(frame_tex, 2.2f) * float3(0.0f, 0.0f, 1.0f) * 4.0f;
  flare += FlareFalloff(frame_tex, 2.0f) * float3(0.02f, 0.0f, 1.0f) * 3.0f;
  flare += FlareFalloff(frame_tex, -0.74f) * float3(1.0f, 0.1f, 0.1f) * 1.0f;
  flare += FlareFalloff(frame_tex, -0.67f) * float3(0.1f, 1.0f, 0.1f) * 1.0f;
  flare += FlareFalloff(frame_tex, -0.6f) * float3(0.0f, 0.0f, 1.0f) * 2.0f;

  o0 = float4(colour + g_lens_flare_trail_amount * flare * ((HDR >= 1.f) ? SI.lens_flare : 1.f), 1.f);
}
