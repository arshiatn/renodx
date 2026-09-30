// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 30 00:43:05 2026

#include "../shared.h"

SamplerState LinearSampler_s : register(s0);
Texture2D<float4> Base1 : register(t0);
Texture2D<float4> Base2 : register(t1);
Texture2D<float4> Base3 : register(t2);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TexCoord0,
  out float4 o0 : SV_Target0)
{
  float4 r0;

  r0.x = Base2.Sample(LinearSampler_s, v1.xy).x;
  r0.xyz = float3(0,-0.391448975,2.01782227) * r0.xxx;
  r0.w = Base3.Sample(LinearSampler_s, v1.xy).x;
  r0.xyz = r0.www * float3(1.59579468,-0.813476563,0) + r0.xyz;
  r0.w = Base1.Sample(LinearSampler_s, v1.xy).x;
  r0.xyz = r0.www * float3(1.16412354,1.16412354,1.16412354) + r0.xyz;
  o0.xyz = float3(-0.87065506,0.529705048,-1.08166885) + r0.xyz;

  if (DOS2_CORRECT_HDR) {
    // Preserve the SDR video's display range before brightness adjustment.
    float3 video_color = saturate(o0.rgb);
    float video_scale = SI.diffuse_white_nits / max(SI.graphics_white_nits, 1.f);
    if (video_scale != 1.f) {
      // ui00 decodes sRGB and multiplies by graphics_white_nits later.
      // Compensate in linear light so that the video follows Paper White.
      video_color = renodx::color::srgb::Encode(
          renodx::color::srgb::Decode(video_color) * video_scale);
    }
    // Do not saturate here: Paper White > UI requires values above 1.
    // The entire intermediate path into ui00 Tex/t1 must preserve that range.
    o0.rgb = video_color;
  }

  o0.w = 1;
  return;
}
