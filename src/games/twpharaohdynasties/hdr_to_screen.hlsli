#ifndef SRC_TWPHARAOHDYNASTIES_HDR_TO_SCREEN_HLSLI_
#define SRC_TWPHARAOHDYNASTIES_HDR_TO_SCREEN_HLSLI_

// hdr_to_screen, every permutation (t01-t04, t05-t08 DOF, photo01-04): the native grade bridge, Extended's grading, PsychoV30 /
// V31. Include after the shader's cbuffers and textures (colour_matrix, lut_texture, lut_strength, g_gamma_output).
#include "./shared.h"
#include "./psycho_test30.hlsli"
#include "./psycho_test31.hlsli"

// The native matrix + LUT are authored for a bounded gamma-domain signal. Compress
// reversibly, grade in that domain, expand back. Same bridge as Troy.
float3 ApplyPharaohNativeGradeHDRSafe(float3 grade_source_linear)
{
  float grade_scale =
      renodx::tonemap::neutwo::ComputeMaxChannelScale(grade_source_linear, 1.f, HDR_PEAK);
  float3 grade_proxy_linear = grade_source_linear * grade_scale;

  // Stock's saturate(g_brightness * c) is dropped: the proxy is already bounded, and
  // re-clamping would only reintroduce clipping ahead of the matrix and LUT.
  float3 grade_proxy_gamma = renodx::color::gamma::EncodeSafe(grade_proxy_linear);

  float4 matrix_input = float4(grade_proxy_gamma, 1.f);
  float3 matrix_output_gamma;
  matrix_output_gamma.x = dot(matrix_input, colour_matrix._m00_m10_m20_m30);
  matrix_output_gamma.y = dot(matrix_input, colour_matrix._m01_m11_m21_m31);
  matrix_output_gamma.z = dot(matrix_input, colour_matrix._m02_m12_m22_m32);

  // No saturate on the LUT coordinate - stock samples the raw matrix output.
  float3 lut_output_gamma =
      lut_texture.SampleLevel(lut_sampler_s, matrix_output_gamma, 0).xyz;
  float3 graded_gamma = lerp(matrix_output_gamma, lut_output_gamma, lut_strength);

  float3 graded_proxy_linear = renodx::color::gamma::DecodeSafe(graded_gamma);
  return renodx::math::DivideSafe(graded_proxy_linear, grade_scale.xxx, graded_proxy_linear);
}


// Mode 1 user grading. Identity at default settings.
float3 ApplyPharaohHDR1UserGrading(float3 color)
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

  // Luma-preserving saturation. 1.0 is identity.
  float graded_y = dot(color, kLuminance);
  return lerp(graded_y.xxx, color, SI.saturation_tpm);
}


// Mode 1 peak management: rolls everything above 1.0 into the display's headroom.
// Runs last, and leaves everything at or below 1.0 alone - that is the SDR image.
float3 ApplyPharaohHDR1PeakShoulder(float3 color)
{
  // Above 1 each channel rolls off to Peak on its own, as SDR clips each channel: bright fire turns yellow / white at
  // its core (as Ryse). (The brightest-channel roll-off kept fire's full orange saturation up to Peak: flat, salmon.)
  const float headroom = max(HDR_PEAK, 1.0001f) - 1.f;
  const float3 over = max(color - 1.f, 0.f);
  return min(color, 1.f)
         + float3(renodx::tonemap::Neutwo(over.r, headroom), renodx::tonemap::Neutwo(over.g, headroom),
                  renodx::tonemap::Neutwo(over.b, headroom));
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
float3 ApplyPharaohPsycho(float3 color)
{
  [branch]
  if (HDR >= 2.5f) {
    return ApplyPsychoV31(color);
  }
  return ApplyPsychoV30(color);
}

// SDR: the stock result, clipped as the original 8-bit target, at Paper White relative to UI (2.2 encoded).
float4 SdrToScreenOutput(float4 stock)
{
  stock = saturate(stock);
  stock.xyz = renodx::color::gamma::DecodeSafe(stock.xyz, SDR_STOCK_GAMMA) * (SI.diffuse_white_nits / SI.graphics_white_nits);
  stock.xyz = renodx::color::gamma::EncodeSafe(stock.xyz);
  return stock;
}

// HDR: the native matrix + LUT on a bounded proxy, then the mode's tone mapper, Paper White relative to UI.
float4 HdrToScreenOutput(float3 color, float alpha)
{
  float3 graded_hdr = ApplyPharaohNativeGradeHDRSafe(max(color, 0.f));
  [branch]
  if (HDR < 1.5f) {
    graded_hdr = ApplyPharaohHDR1UserGrading(graded_hdr);
    graded_hdr = ApplyPharaohHDR1PeakShoulder(graded_hdr);
  } else {
    graded_hdr = ApplyPharaohPsycho(graded_hdr);
  }
  return float4(renodx::color::gamma::EncodeSafe(graded_hdr * (SI.diffuse_white_nits / SI.graphics_white_nits)), alpha);
}

#endif  // SRC_TWPHARAOHDYNASTIES_HDR_TO_SCREEN_HLSLI_
