#ifndef SRC_TWTHRONESOFBRITANNIA_TONE_MAPPER_HLSLI_
#define SRC_TWTHRONESOFBRITANNIA_TONE_MAPPER_HLSLI_

// hdr_tone_mapper's tone mappers, HDR (basic: the auto exposure's black / white points; constant and constant with
// bloom: fixed ones; Attila's family): adaptive black / white exposure and the per-channel S-curve kept as grading,
// without the SDR highlight cap, forced whitening and final clamp. Include after the shader's cbuffers.
#include "./shared.h"
#include "../twattila/psycho35.hlsli"

// `stock_highlight_limit`: the stock path limits the scene's highlights before bloom (t00, t17; not t18), for Hue Clip.
float4 ToneMapperHdr(float4 scene_sample, float4 bloom_sample, float2 black_white_points, bool stock_highlight_limit = true)
{
  // Stock bloom strength is 1.0. SI.bloom is an intuitive HDR multiplier.
  const float3 bloom_color = bloom_sample.xyz * SI.bloom;
  float3 hdr_color =
      max(scene_sample.xyz + bloom_color, 0.f.xxx);

  float black_point = black_white_points.x;
  float white_point = black_white_points.y;
  float black_white_range =
      max(white_point - black_point, 0.000001f);

  // Preserve Britannia's adaptive black/white-point exposure without its
  // upper SDR clamp.
  float hdr_luminance =
      dot(hdr_color, float3(0.212599993f, 0.715200007f, 0.0722000003f));

  float normalized_hdr_luminance =
      max(hdr_luminance - black_point, 0.f) / black_white_range;

  float normalization_scale =
      hdr_luminance > 0.f
          ? normalized_hdr_luminance / hdr_luminance
          : 1.f;

  float3 untonemapped = hdr_color * normalization_scale;

  // PsychoV35: Hue Clip Emulation before the S-curve (twattila/psycho35.hlsli; Attila's engine).
  [branch]
  if (HDR >= 3.f && SI.hue_clip > 0.f) {
    untonemapped = renodx::tonemap::psychov::AttilaEngineHueClip(untonemapped, normalized_hdr_luminance, scene_sample.xyz,
                                                                 bloom_color, black_point, white_point, stock_highlight_limit,
                                                                 SI.hue_clip);
  }

  // Britannia/Attila's S-curve is a per-channel SDR curve. Compress the HDR
  // signal into its valid domain, apply the native curve, then undo only the
  // neutral scale so the native artistic grade survives into HDR.
  float curve_scale =
      renodx::tonemap::neutwo::ComputeMaxChannelScale(untonemapped);

  float3 curve_input = untonemapped * curve_scale;

  float3 graded_proxy;
  graded_proxy.r =
      g_scurve_texture_sampler.Sample(
          g_scurve_texture_sampler_s, float2(curve_input.r, 0.5f)).r;
  graded_proxy.g =
      g_scurve_texture_sampler.Sample(
          g_scurve_texture_sampler_s, float2(curve_input.g, 0.5f)).g;
  graded_proxy.b =
      g_scurve_texture_sampler.Sample(
          g_scurve_texture_sampler_s, float2(curve_input.b, 0.5f)).b;

  float3 graded_hdr =
      renodx::math::DivideSafe(
          graded_proxy,
          curve_scale.xxx,
          graded_proxy);

  return float4(graded_hdr, scene_sample.w);
}

#endif  // SRC_TWTHRONESOFBRITANNIA_TONE_MAPPER_HLSLI_
