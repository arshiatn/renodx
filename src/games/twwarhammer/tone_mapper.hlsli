#ifndef SRC_TWWARHAMMER_TONE_MAPPER_HLSLI_
#define SRC_TWWARHAMMER_TONE_MAPPER_HLSLI_

// hdr_tone_mapper's tone mappers, HDR (basic: the auto exposure's black / white points; constant and constant with
// bloom: fixed ones): Warhammer's adaptive exposure and S-curve kept as grading, without the stock whiteout /
// desaturation and upper SDR clamp. Include after the shader's cbuffers (g_scurve_texture).
#include "./shared.h"

// Literally the same as Attila
float3 ApplyWarhammerSCurve(float3 color)
{
  float3 output;
  output.r = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.r, 0.5f)).r;
  output.g = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.g, 0.5f)).g;
  output.b = g_scurve_texture.Sample(g_scurve_texture_sampler_s, float2(color.b, 0.5f)).b;
  return output;
}

float4 ToneMapperHdr(float4 scene_sample, float4 bloom_sample, float2 black_white_points)
{
  float3 hdr_color = max(scene_sample.xyz + bloom_sample.xyz, 0.f.xxx);

  // Keep the game's adaptive black/white-point exposure, but do not upper
  // clamp luminance to the white point. This retains highlight headroom.

  float black_point = black_white_points.x;
  float white_point = black_white_points.y;
  float black_white_range = max(white_point - black_point, 0.000001f);

  float hdr_luminance = dot(hdr_color, float3(0.212599993f, 0.715200007f, 0.0722000003f));
  float normalized_hdr_luminance = max(hdr_luminance - black_point, 0.f) / black_white_range;

  float hdr_normalization_scale = hdr_luminance > 0.f ? normalized_hdr_luminance / hdr_luminance : 1.f;
  float3 untonemapped = hdr_color * hdr_normalization_scale;

  // Warhammer's S-curve is per-channel, like Attila's. Compress the unclipped signal into the LUT's valid 0-1 domain, apply the native
  // curve as artistic grading, then undo the same scalar compression.
  float curve_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(untonemapped);
  float3 curve_input = untonemapped * curve_scale;
  float3 graded_proxy = ApplyWarhammerSCurve(curve_input);
  float3 graded_hdr = renodx::math::DivideSafe(graded_proxy, curve_scale.xxx, graded_proxy);

  return float4(graded_hdr, scene_sample.w);
}

#endif  // SRC_TWWARHAMMER_TONE_MAPPER_HLSLI_
