#ifndef SRC_TWROME2_TONE_MAPPER_HLSLI_
#define SRC_TWROME2_TONE_MAPPER_HLSLI_

// hdr_tone_mapper's tone mappers (basic: the auto exposure's black / white points; constant and constant with bloom:
// fixed ones), their HDR path. Include after the shader's cbuffers (camera, vignette, S-curve texture).
#include "./shared.h"
#include "../twattila/psycho35.hlsli"

// 1) Scene x vignette (Vignette slider, only with the game's vignette on) + bloom x Bloom slider.
// 2) The stock black subtraction and exposure normalization, without the clamp to the white point and the 0.011 black
//    floor.
// 3) Rome II's tone curve (the S-curve texture over log luminance between the points) on a Neutwo-compressed luminance
//    proxy (Rome II grades scalar log luminance: closer to it than an RGB proxy), expanded back with the proxy's scale.
// black_white_points: log10 black, black, log10 white, white.
float4 ToneMapperHdr(float4 scene_sample, float4 bloom_sample, float4 black_white_points, float2 position)
{
  const float3 luminance_weights = float3(0.212599993f, 0.715200007f, 0.0722000003f);

  const float vignette_position = g_vignette_enable * g_recip_half_screen_diag * length(position - g_screen_size.xy * 0.5f);
  const float vignette_position_2 = vignette_position * vignette_position;
  float vignette_scale = 1.f + 1.60193861f * vignette_position_2 * vignette_position_2
                         - 3.24679637f * vignette_position_2 * vignette_position + 1.24311411f * vignette_position_2
                         - 0.219172657f * vignette_position;
  if (g_vignette_enable == 1) {
    // Scales the darkening (as a lerp up to 1), clamped so 2 can reach black.
    vignette_scale = 1.f - saturate((1.f - vignette_scale) * SI.vignette);
  }
  float3 hdr_color = max(scene_sample.xyz * vignette_scale, 0.f);
  hdr_color = max(hdr_color + bloom_sample.xyz * SI.bloom, 0.f);

  const float linear_luminance_range = max(black_white_points.w - black_white_points.y, 0.000001f);
  const float hdr_luminance = dot(hdr_color, luminance_weights);
  const float normalized_luminance = max(hdr_luminance - black_white_points.y, 0.f) / linear_luminance_range;
  float3 untonemapped = (hdr_luminance > 0.f ? hdr_color * (normalized_luminance / hdr_luminance) : 0.f.xxx);
  const float log_luminance_range = max(black_white_points.z - black_white_points.x, 0.000001f);

  // PsychoV35: Hue Clip Emulation before the S-curve (twattila/psycho35.hlsli), toward the hue Rome II's SDR picture
  // has: the colour at the luminance the stock path gives it (the scene's clamped at the white point, bloom, the S-curve),
  // each channel clipped at 1.
  [branch]
  if (HDR >= 3.f && SI.hue_clip > 0.f) {
    const float stock_luminance = max(min(dot(scene_sample.xyz * vignette_scale, luminance_weights), black_white_points.w)
                                          + dot(bloom_sample.xyz * SI.bloom, luminance_weights),
                                      0.000001f);
    const float stock_coordinate = (log2(stock_luminance) * 0.30103001f - black_white_points.x) / log_luminance_range;
    const float stock_curve = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(stock_coordinate, 0.5f)).x;
    const float stock_mapped = (exp2((stock_curve * log_luminance_range + black_white_points.x) * 3.32192802f) - black_white_points.y)
                               / linear_luminance_range;
    untonemapped = renodx::tonemap::psychov::HueClip(
        untonemapped, renodx::math::DivideSafe(max(stock_mapped, 0.f), normalized_luminance, 0.f), SI.hue_clip);
  }

  const float curve_scale = (normalized_luminance > 0.f
                                 ? renodx::tonemap::Neutwo(normalized_luminance) / normalized_luminance
                                 : 1.f);
  const float3 curve_input = untonemapped * curve_scale;
  const float curve_input_luminance = normalized_luminance * curve_scale;
  const float curve_domain_luminance = max(black_white_points.y + curve_input_luminance * linear_luminance_range, 0.000001f);
  const float curve_coordinate = (log2(curve_domain_luminance) * 0.30103001f - black_white_points.x) / log_luminance_range;
  const float curve_value = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(curve_coordinate, 0.5f)).x;
  const float mapped_luminance = exp2((curve_value * log_luminance_range + black_white_points.x) * 3.32192802f);
  const float graded_proxy_luminance = max((mapped_luminance - black_white_points.y) / linear_luminance_range, 0.f);
  const float3 graded_proxy = (curve_input_luminance > 0.f
                                   ? curve_input * (graded_proxy_luminance / curve_input_luminance)
                                   : 0.f.xxx);
  return float4(renodx::math::DivideSafe(graded_proxy, curve_scale.xxx, graded_proxy),
                saturate(scene_sample.w + bloom_sample.w));
}

#endif  // SRC_TWROME2_TONE_MAPPER_HLSLI_
