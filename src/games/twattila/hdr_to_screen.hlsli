#ifndef SRC_TWATTILA_HDR_TO_SCREEN_HLSLI_
#define SRC_TWATTILA_HDR_TO_SCREEN_HLSLI_

// hdr_to_screen, every permutation: the game's last pass (fog of war, colour matrix, brightness, gamma), its HDR and
// SDR ends. Include after the shader's cbuffers (colour_matrix, g_gamma_output).
#include "./shared.h"
#include "./psycho_test30.hlsli"
#include "./psycho_test31.hlsli"

// PsychoV31, anchored at mid gray (Attila's S-curve runs before, in t00); target-only where its source cage returns
// black. Highlight Hue / Saturation Shift: toward the SDR image's, which clips each channel at white (bright fire turns
// orange / yellow there, V31 keeps the scene's hue), above mid gray, fully from white. Last, what the shifts pushed out
// of the target gamut is brought back.
float3 ApplyPsychoV31(float3 color)
{
  // SDR output: BT.709 (the proxy clips per channel).
  const int target_gamut = (ATTILA_SDR_OUTPUT ? renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT709
                                               : int(SI.v31_gamut_compression_mode));
  const float compression = 1.f + 0.5f * SI.v31_roll_off * SI.v31_roll_off;
  float3 result = renodx::tonemap::psychov::custom_psychotm_test31(
      color, HDR_PEAK, SI.v31_exposure, SI.v31_highlights, SI.v31_shadows, SI.v31_contrast, SI.v31_flare,
      SI.v31_highlight_contrast, SI.v31_shadow_contrast, SI.v31_saturation, SI.v31_highlight_saturation,
      SI.v31_dechroma, 0.18f, 0.18f, 0.f, SI.v31_gamut_compression, target_gamut, compression, 1.f, 0.5f, 0.f,
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 1.f);
  const float3 exposed = color * SI.v31_exposure;
  const float scene_y = renodx::color::y::from::BT709(exposed);
  [branch]
  if (all(result == 0.f) && scene_y > 9.999015e-7f) {
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

// HDR: the colour matrix (made for the clipped SDR image, gamma 2.0 = stock at in-game gamma 20) on a copy compressed
// into 0 - 1 (Neutwo by max channel, its exact inverse after), PsychoV30 or V31, Paper White relative to UI, 2.0 encoded.
float4 HdrToScreenOutput(float3 hdr_color, float alpha)
{
  const float matrix_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(hdr_color, 1.f, HDR_PEAK);
  const float4 matrix_input = float4(renodx::color::gamma::EncodeSafe(hdr_color * matrix_scale, 2.f), 1.f);
  const float3 matrix_output_gamma = float3(dot(matrix_input, colour_matrix._m00_m10_m20_m30),
                                            dot(matrix_input, colour_matrix._m01_m11_m21_m31),
                                            dot(matrix_input, colour_matrix._m02_m12_m22_m32));
  const float3 graded_shaped = renodx::color::gamma::DecodeSafe(matrix_output_gamma, 2.f);
  const float3 graded_hdr = renodx::math::DivideSafe(graded_shaped, matrix_scale.xxx, graded_shaped);
  float3 color;
  [branch]
  if (HDR >= 2.f) {
    color = ApplyPsychoV31(graded_hdr);
  } else {
    color = ApplyAttilaPsychoV30(graded_hdr);
  }
  color *= ATTILA_PAPER_WHITE;
  return float4(renodx::color::gamma::EncodeSafe(color, 2.f), saturate(alpha));
}

// SDR: the stock result, clipped as the original 8-bit target, at Paper White relative to UI. Gamma Correction starts
// from gamma 2.0 whatever the in-game Gamma (the proxy takes it to 2.2); off, the in-game Gamma stays.
float4 SdrToScreenOutput(float4 stock)
{
  stock = saturate(stock);
  stock.xyz = renodx::color::gamma::DecodeSafe(stock.xyz, g_gamma_output) * ATTILA_PAPER_WHITE;
  stock.xyz = renodx::color::gamma::EncodeSafe(stock.xyz, (SI.gamma_correction != 0.f) ? 2.f : g_gamma_output);
  return stock;
}

#endif  // SRC_TWATTILA_HDR_TO_SCREEN_HLSLI_
