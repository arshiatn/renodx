#ifndef SRC_TWATTILA_TONE_MAPPER_HLSLI_
#define SRC_TWATTILA_TONE_MAPPER_HLSLI_

// hdr_tone_mapper's tone mappers (basic: the auto exposure's black / white points; constant and constant with bloom:
// fixed ones). Include after the shader's cbuffers (camera, S-curve texture).
#include "./shared.h"
#include "./psycho_test31.hlsli"

// Attila's S-curve texture, per channel (stock).
float3 ApplyAttilaSCurve(float3 color)
{
  float3 output;
  output.r = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.r, 0.5f)).r;
  output.g = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.g, 0.5f)).g;
  output.b = g_scurve_texture_sampler.Sample(g_scurve_texture_sampler_s, float2(color.b, 0.5f)).b;
  return output;
}

// HDR: scene + bloom x Bloom slider, Attila's black / white point exposure without its upper clamp, highlight clamp
// and forced desaturation, then its S-curve as a grade on a Neutwo-compressed copy, expanded back (PsychoV30 / V31 /
// V35 tone map in hdr_to_screen). `stock_highlight_limit`: the stock path limits the scene's highlights before bloom
// (t00, t09; not t10).
float4 ToneMapperHdr(float4 scene_sample, float4 bloom_sample, float2 black_white_points,
                     bool stock_highlight_limit = true)
{
  float output_alpha = scene_sample.w;
  // Attila adds this texture at full strength. SI.bloom therefore uses an
  // intuitive scale: 0 disables bloom, 1 is stock, and 2 is twice stock.
  float3 bloom_color = bloom_sample.xyz * SI.bloom;

  // Capture scene+bloom before Attila's SDR highlight clamp and forced
  // desaturation. This is the signal that the later PsychoV30 pass tone maps.
  float3 hdr_color = scene_sample.xyz + bloom_color;

  // Preserve Attila's adaptive black/white-point exposure, but do not apply an
  // upper clamp. Highlights and bloom therefore remain available to PsychoV30.
  float black_point = black_white_points.x;
  float white_point = black_white_points.y;
  float black_white_range = max(white_point - black_point, 0.000001f);

  float hdr_luminance = dot(hdr_color, float3(0.212599993, 0.715200007, 0.0722000003));
  float normalized_hdr_luminance =max(hdr_luminance - black_point, 0.f) / black_white_range;
  float hdr_normalization_scale = hdr_luminance > 0.f? normalized_hdr_luminance / hdr_luminance : 1.f;
  float3 untonemapped = hdr_color * hdr_normalization_scale;

  // PsychoV35: Hue Clip before the S-curve, toward the hue Attila's SDR picture has: each channel clipped at the white
  // point (1 here) at the luminance the stock path gives the pixel (its highlight limit, bloom, the white clamp).
  [branch]
  if (HDR >= 3.f && SI.hue_clip > 0.f) {
    const float scene_luminance = dot(scene_sample.xyz, float3(0.212599993, 0.715200007, 0.0722000003));
    const float scene_limit = (stock_highlight_limit ? black_white_range * -0.340000004f + white_point : scene_luminance);
    const float stock_luminance = min(scene_luminance, scene_limit)
                                  + dot(bloom_color, float3(0.212599993, 0.715200007, 0.0722000003));
    const float stock_normalized_luminance = saturate((stock_luminance - black_point) / black_white_range);
    untonemapped = renodx::tonemap::psychov::HueClip(
        untonemapped, renodx::math::DivideSafe(stock_normalized_luminance, normalized_hdr_luminance, 0.f), SI.hue_clip);
  }

  // Retain Attila's S-curve as a color grade, not as the final tone mapper.
  // NeuTwo moves the unclipped signal into the LUT's 0-1 domain without the
  // per-channel saturation that created white bloom spots. Undoing the same
  // scalar afterwards restores the HDR range for PsychoV30 in t01-t08.
  float lut_scale =renodx::tonemap::neutwo::ComputeMaxChannelScale(untonemapped);
  float3 lut_input = untonemapped * lut_scale;
  float3 graded_hdr = ApplyAttilaSCurve(lut_input);
  graded_hdr = renodx::math::DivideSafe(graded_hdr, lut_scale.xxx, graded_hdr);

  return float4(graded_hdr, saturate(output_alpha));  // UNORM clamp lost to the FP16 upgrade
}

#endif  // SRC_TWATTILA_TONE_MAPPER_HLSLI_
