#ifndef SRC_TWWARHAMMER_HDR_TO_SCREEN_HLSLI_
#define SRC_TWWARHAMMER_HDR_TO_SCREEN_HLSLI_

// hdr_to_screen, every permutation (t01-t08): the HDR end. Include after the shader's cbuffers and textures
// (colour_matrix, lut_texture_1-3, lut_sampler_1-3_s, lut_weights).
#include "./shared.h"
#include "./psycho_test30.hlsli"
#include "./psycho_test31.hlsli"

// PsychoV31, anchored at mid gray (Warhammer's S-curve runs before, in t00); target-only where its source cage returns
// black. Highlight Hue / Saturation Shift: toward the SDR image's, which clips each channel at white (bright fire turns
// orange / yellow there, V31 keeps the scene's hue), above mid gray, fully from white. Last, what the shifts pushed out
// of the target gamut is brought back.
float3 ApplyPsychoV31(float3 color)
{
  const int target_gamut = int(SI.v31_gamut_compression_mode);
  const float compression = 1.f + 0.5f * SI.v31_roll_off * SI.v31_roll_off;
  float3 result = renodx::tonemap::psychov::custom_psychotm_test31(
      color, HDR_PEAK, SI.v31_exposure, SI.v31_highlights, SI.v31_shadows, SI.v31_contrast, SI.v31_flare,
      SI.v31_highlight_contrast, SI.v31_shadow_contrast, SI.v31_saturation, SI.v31_highlight_saturation,
      SI.v31_dechroma, 0.18f, 0.18f, 0.f, SI.v31_gamut_compression, target_gamut, compression, 1.f, 0.5f, 0.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 1.f);
  const float3 exposed = color * SI.v31_exposure;
  const float scene_y = renodx::color::y::from::BT709(exposed);
  [branch]
  if (all(result == 0.f) && scene_y > 9.999621e-7f) {
    result = renodx::tonemap::psychov::custom_psychotm_test31(
        color, HDR_PEAK, SI.v31_exposure, SI.v31_highlights, SI.v31_shadows, SI.v31_contrast, SI.v31_flare,
        SI.v31_highlight_contrast, SI.v31_shadow_contrast, SI.v31_saturation, SI.v31_highlight_saturation,
        SI.v31_dechroma, 0.18f, 0.18f, 0.f, SI.v31_gamut_compression, target_gamut, compression, 1.f, 0.5f, 0.f,
        renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 0.f);
  }
  [branch]
  if ((SI.v31_highlight_hue_shift > 0.f || SI.v31_highlight_saturation_shift > 0.f) && scene_y > 0.18001847f) {
    const float t = saturate(log2(scene_y / 0.18f) / log2(1.f / 0.18f));
    const float weight = t * t * (3.f - 2.f * t);
    const float3 sdr = saturate(exposed);
    result = renodx::color::correct::Hue(result, sdr, SI.v31_highlight_hue_shift * weight);
    [branch]
    if (SI.v31_highlight_saturation_shift > 0.f) {
      result = renodx::tonemap::psychov::ShiftSaturationToward(result, sdr, SI.v31_highlight_saturation_shift * weight);
    }
  }
  [branch]
  if (SI.v31_gamut_compression > 0.f) {
    result = renodx::tonemap::psychov::FitToTargetGamut(result, target_gamut);
  }
  return result;
}

// HDR: the native matrix + three LUTs on a bounded proxy, expanded back, PsychoV30 or V31, Paper White relative to UI.
float4 HdrToScreenOutput(float3 hdr_color, float output_alpha)
{
  // The stock matrix and 3D LUTs expect bounded gamma-domain coordinates.
  // Compress the HDR signal reversibly, apply both native grading stages in
  // their expected domain, then expand back to HDR before PsychoV30.
  float grade_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(hdr_color, 1.f, HDR_PEAK);
  float3 grade_input_linear = hdr_color * grade_scale;
  float3 grade_input_gamma =  renodx::color::gamma::EncodeSafe(grade_input_linear);

  float4 matrix_input = float4(grade_input_gamma, 1.f);
  float3 matrix_output_gamma;
  matrix_output_gamma.x = dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y = dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z = dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  //Tried to use renodx LUT and copying it from Total War Three Kingdoms but no matter what that was changing the colors and etc. sao I am sticking to  game's SDR way.
  // scaled down -> matrix color -> lut -> scale up (which is probably not 100% correct scale because LUT can change it)
  float3 lut_1 = lut_texture_1.SampleLevel(lut_sampler_1_s, matrix_output_gamma, 0).xyz;
  float3 lut_2 = lut_texture_2.SampleLevel(lut_sampler_2_s, matrix_output_gamma, 0).xyz;
  float3 lut_3 = lut_texture_3.SampleLevel(lut_sampler_3_s, matrix_output_gamma, 0).xyz;

  float3 graded_gamma = lut_1 * lut_weights[0] + lut_2 * lut_weights[1] + lut_3 * lut_weights[2];

  float3 graded_proxy = renodx::color::gamma::DecodeSafe(graded_gamma);
  float3 graded_hdr = renodx::math::DivideSafe(graded_proxy, grade_scale.xxx, graded_proxy);

  float3 color;
  [branch]
  if (HDR >= 2.f) {
    color = ApplyPsychoV31(graded_hdr);
  } else {
    color = ApplyPsychoV30(graded_hdr);
  }
  return float4(renodx::color::gamma::EncodeSafe(color * (SI.diffuse_white_nits / SI.graphics_white_nits)), output_alpha);
}

#endif  // SRC_TWWARHAMMER_HDR_TO_SCREEN_HLSLI_
