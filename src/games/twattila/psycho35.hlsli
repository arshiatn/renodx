#ifndef SRC_TWATTILA_PSYCHO35_HLSLI_
#define SRC_TWATTILA_PSYCHO35_HLSLI_

// PsychoV35 for every game that has it: Musa's PsychoV35 as they all call it, and Hue Clip Emulation. Include after the
// game's shared.h (renodx.hlsl, SI with the v35_* fields and hue_clip).
#include "./psycho_test35.hlsli"

namespace renodx {
namespace tonemap {
namespace psychov {

// Hue Clip (PsychoV35, before the game's grade): the middle channel moved toward the place the game's per-channel SDR
// clip gives it (`clip_scale` brings the colour to the game's clip at 1; Neutwo per channel is that clip made smooth,
// so the bend has no edge where every channel clips). Signs, the smallest and the largest magnitude are kept, then the
// BT.709 luminance. `strength` 0 none, 1 the clip's place. Sky blue (blue the largest, red the smallest) twice as far,
// never past the largest channel: the games' SDR pictures turn sky cyan more than fire yellow (test/tw_psychov35).
// HueTransfer: the same toward any `reference` colour (a game's own per-channel SDR result, Troy's and Pharaoh's ACES
// fit); HueClip: toward the clip's.
float3 HueTransfer(float3 color_bt709, float3 reference, float strength) {
  const float3 magnitude = abs(color_bt709);
  const float reference_min = renodx::math::Min(reference);
  const float reference_range = renodx::math::Max(reference) - reference_min;
  if (reference_range == 0.f) return color_bt709;
  const float magnitude_min = renodx::math::Min(magnitude);
  const float magnitude_max = renodx::math::Max(magnitude);
  const float3 transferred = lerp(magnitude_min, magnitude_max, (reference - reference_min) / reference_range);
  const bool sky = magnitude.b >= magnitude.g && magnitude.g >= magnitude.r;
  const float3 bent = renodx::math::CopySign(
      clamp(lerp(magnitude, transferred, (sky ? 2.f : 1.f) * strength), magnitude_min, magnitude_max), color_bt709);
  const float original_y = renodx::color::y::from::BT709(color_bt709);
  const float bent_y = renodx::color::y::from::BT709(bent);
  return (original_y > 0.f && bent_y > 0.f) ? bent * (original_y / bent_y) : color_bt709;
}

float3 HueClip(float3 color_bt709, float clip_scale, float strength) {
  return HueTransfer(color_bt709, renodx::tonemap::neutwo::PerChannel(abs(color_bt709) * clip_scale), strength);
}

// Attila's engine (Attila, Thrones): Hue Clip before the S-curve, toward the hue its SDR picture has: each channel
// clipped at the white point (1 here) at the luminance the stock path gives the pixel (its highlight limit before
// bloom, bloom, the white clamp). `untonemapped`: the scene + bloom between the black and white points, its luminance
// `normalized_luminance`; `stock_highlight_limit`: the stock path limits the scene's highlights before bloom.
float3 AttilaEngineHueClip(float3 untonemapped, float normalized_luminance, float3 scene, float3 bloom, float black_point,
                           float white_point, bool stock_highlight_limit, float strength) {
  const float black_white_range = max(white_point - black_point, 0.000001f);
  const float scene_luminance = dot(scene, float3(0.212599993f, 0.715200007f, 0.0722000003f));  // the stock weights
  const float scene_limit = (stock_highlight_limit ? black_white_range * -0.340000004f + white_point : scene_luminance);
  const float stock_luminance = min(scene_luminance, scene_limit) + dot(bloom, float3(0.212599993f, 0.715200007f, 0.0722000003f));
  const float stock_normalized_luminance = saturate((stock_luminance - black_point) / black_white_range);
  return HueClip(untonemapped, renodx::math::DivideSafe(stock_normalized_luminance, normalized_luminance, 0.f), strength);
}

}  // namespace psychov
}  // namespace tonemap
}  // namespace renodx

// Musa's PsychoV35 with its own sliders (SI.v35_*), anchored at mid gray. `peak`: Peak over Paper White; `sdr_output`:
// an SDR display (fitted into BT.709, else BT.2020). Highlight Roll-off: 0 latest, 1 Musa's compression (1.5), 2 soonest.
// `mid_gray`: where the scene's 0.18 lands (a game's own curve's), `contrast_scale`: Contrast's (a game's own per map).
float3 ApplyPsychoV35(float3 color, float peak, bool sdr_output, float mid_gray = 0.18f, float contrast_scale = 1.f) {
  const float3 mapped = renodx::tonemap::psychov::custom_psychotm_test35(
      color, peak, SI.v35_exposure, SI.v35_highlights, SI.v35_shadows, max(SI.v35_contrast, 0.01f) * contrast_scale,
      SI.v35_flare, SI.v35_highlight_contrast, SI.v35_shadow_contrast, SI.v35_saturation, SI.v35_highlight_saturation,
      SI.v35_dechroma, 0.18f, mid_gray, 0.f, SI.v35_gamut_compression, sdr_output ? 0 : 1, 1.f + 0.5f * SI.v35_roll_off);
  // A NaN left (a low Shadows' darkest pixels): black. A bit test, as fxc folds isnan (renodx::math::ZeroNaN) away; SM3
  // (RUSE's DX9 shaders) has no bit casts.
#if __SHADER_TARGET_MAJOR >= 4
  return ((asuint(mapped) & 0x7fffffffu) > 0x7f800000u) ? 0.f : mapped;
#else
  return mapped;
#endif
}

#endif  // SRC_TWATTILA_PSYCHO35_HLSLI_
