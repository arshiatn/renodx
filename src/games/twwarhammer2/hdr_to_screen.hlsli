#ifndef SRC_TWWARHAMMER2_HDR_TO_SCREEN_HLSLI_
#define SRC_TWWARHAMMER2_HDR_TO_SCREEN_HLSLI_

// hdr_to_screen, every permutation (t01 / t02 and t03-t08): the native grade bridge, Extended's grading, PsychoV30 /
// V31, and the SDR / HDR ends. Include after the shader's cbuffers and textures (colour_matrix, lut_texture_1-3,
// lut_sampler_1-3_s, lut_weights, g_brightness, g_gamma_output, g_inv_gamma_output).
#include "./shared.h"
#include "./psycho_test30.hlsli"
#include "./psycho_test31.hlsli"

float3 ApplyHDR1UserGrading(float3 color)
{
  const float3 kLuminance = float3(0.212599993f, 0.715200007f, 0.0722000003f);
  const float mid_gray = 0.18f;

  float luminance = max(dot(color, kLuminance), 0.f);
  float graded_luminance = luminance * SI.exposure_tpm;
  graded_luminance = renodx::color::grade::Highlights(graded_luminance, SI.highlights_tpm, mid_gray);
  graded_luminance = renodx::color::grade::Shadows(graded_luminance, SI.shadows_tpm, mid_gray);

  if (SI.contrast_tpm != 1.f)
  {
    float normalized = max(graded_luminance / mid_gray, 0.f);
    graded_luminance = pow(normalized, SI.contrast_tpm) * mid_gray;
  }

  float tonal_scale = luminance > 0.000001f ? graded_luminance / luminance : 1.f;
  color *= tonal_scale;

  // Luma-preserving saturation control. 1.0 is identity.
  float graded_y = dot(color, kLuminance);
  return lerp(graded_y.xxx, color, SI.saturation_tpm);
}

float3 ApplyHDR1PeakShoulder(float3 color)
{
  // Above 1 each channel rolls off to Peak on its own, as SDR clips each channel: bright fire turns yellow / white at
  // its core (as Ryse). (The brightest-channel roll-off kept fire's full orange saturation up to Peak: flat, salmon.)
  const float headroom = max(HDR_PEAK, 1.0001f) - 1.f;
  const float3 over = max(color - 1.f, 0.f);
  return min(color, 1.f)
         + float3(renodx::tonemap::Neutwo(over.r, headroom), renodx::tonemap::Neutwo(over.g, headroom),
                  renodx::tonemap::Neutwo(over.b, headroom));
}

float3 ApplyNativeGradeHDRSafe(float3 grade_source_linear)
{
  // The game's 3D LUTs are authored for the SDR 0..1 domain. For HDR values,
  // normalize only when necessary, perform the native grade on that proxy, then
  // restore the neutral magnitude. This preserves the LUT's artistic color work
  // without feeding out-of-domain HDR coordinates into the LUT.
  // g_brightness is applied before the LUT, so the check includes it (no flat spot if raised).
  float grade_max_channel = g_brightness * max(grade_source_linear.r, max(grade_source_linear.g, grade_source_linear.b));
  float grade_scale = (grade_max_channel > 1.f) ? rcp(grade_max_channel) : 1.f;
  float3 grade_proxy_linear = grade_source_linear * grade_scale;

  // Match the native SDR pre-LUT brightness and gamma controls rather than
  // assuming a fixed 2.2 transfer.
  float3 grade_proxy_gamma = pow(saturate(g_brightness * grade_proxy_linear), g_inv_gamma_output.xxx);
  float4 matrix_input = float4(grade_proxy_gamma, 1.f);

  float3 matrix_output_gamma;
  matrix_output_gamma.x = dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y = dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z = dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  float3 lut_1 = lut_texture_1.SampleLevel(lut_sampler_1_s, matrix_output_gamma, 0).xyz;
  float3 lut_2 = lut_texture_2.SampleLevel(lut_sampler_2_s, matrix_output_gamma, 0).xyz;
  float3 lut_3 = lut_texture_3.SampleLevel(lut_sampler_3_s, matrix_output_gamma, 0).xyz;
  float3 graded_gamma = lut_1 * lut_weights[0] + lut_2 * lut_weights[1] + lut_3 * lut_weights[2];

  float3 graded_proxy_linear = renodx::color::gamma::DecodeSafe(graded_gamma, g_gamma_output);
  return renodx::math::DivideSafe(graded_proxy_linear, grade_scale.xxx, graded_proxy_linear);
}

// PsychoV31, on t00's scene-linear handoff (mid gray at 0.18); target-only where its source cage returns
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
  if (all(result == 0.f) && scene_y > 1e-6f) {
    result = renodx::tonemap::psychov::custom_psychotm_test31(
        color, HDR_PEAK, SI.v31_exposure, SI.v31_highlights, SI.v31_shadows, SI.v31_contrast, SI.v31_flare,
        SI.v31_highlight_contrast, SI.v31_shadow_contrast, SI.v31_saturation, SI.v31_highlight_saturation,
        SI.v31_dechroma, 0.18f, 0.18f, 0.f, SI.v31_gamut_compression, target_gamut, compression, 1.f, 0.5f, 0.f,
        renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 0.f);
  }
  [branch]
  if ((SI.v31_highlight_hue_shift > 0.f || SI.v31_highlight_saturation_shift > 0.f) && scene_y > 0.18f) {
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

// The Psycho modes: PsychoV30 (Rendering Mode 2) or PsychoV31 (3).
float3 ApplyWarhammer2Psycho(float3 color)
{
  [branch]
  if (HDR >= 2.5f) {
    return ApplyPsychoV31(color);
  }
  return ApplyPsychoV30(color);
}

// SDR: the stock result at Paper White relative to UI (2.2 encoded), as t01.
float4 SdrToScreenOutput(float4 stock)
{
  stock.xyz = renodx::color::gamma::DecodeSafe(stock.xyz, SDR_STOCK_GAMMA) * SDR_PAPER_WHITE;
  stock.xyz = renodx::color::gamma::EncodeSafe(stock.xyz);
  return stock;
}

// HDR: the native grade on a bounded proxy, then Extended's grading and shoulder or PsychoV30 / V31.
float4 HdrToScreenOutput(float3 color, float alpha)
{
  float3 graded_hdr = ApplyNativeGradeHDRSafe(color);
  [branch]
  if (HDR < 1.5f) {
    graded_hdr = ApplyHDR1UserGrading(graded_hdr);
    graded_hdr = ApplyHDR1PeakShoulder(graded_hdr);
  } else {
    graded_hdr = ApplyWarhammer2Psycho(graded_hdr);
  }
  return float4(renodx::color::gamma::EncodeSafe(graded_hdr * (SI.diffuse_white_nits / SI.graphics_white_nits)), alpha);
}

#endif  // SRC_TWWARHAMMER2_HDR_TO_SCREEN_HLSLI_
