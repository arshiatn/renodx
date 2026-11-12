#ifndef SRC_TWROMEREMASTERED_RESOLVE_HLSLI_
#define SRC_TWROMEREMASTERED_RESOLVE_HLSLI_

// ResolveMainColor.hlsl PSMain, every permutation (FeralShaders: 12 distinct). Each shader declares its resources
// (t0 scene, t1 auto exposure, t2 + s5_s colour LUT, t3 dither noise, cb0), defines its features and includes this:
//   RMC_TONEMAP   auto exposure + Hable (the game's tone mapping on); 0: the scene as stored, SDR in every mode
//   RMC_LUT       colour LUT (t2), strength cb0[4].w
//   RMC_VIGNETTE  elliptical vignette (cb0[2], cb0[3].xy)
//   RMC_TINT      over the vignette: a horizontal gradient (cb0[3].zw) and a colour (cb0[4].xyz)
//
// ROME REMASTERED t00 (all features but the tint) - tonemapper.
// Stock: decode 2.2 -> auto exposure (t1) -> Hable on luminance (textbook constants,
// white point from auto exposure) -> gamma cb0[1].x -> LUT -> brightness -> vignette
// -> 8-bit dither.
// HDR 0: stock, then paper white.
// HDR 1 (Extended): Musa's UC2 extended with its default pivot (third-derivative root,
//        ~7% of white): stock Hable below, straight line above. LUT in a bounded proxy,
//        user grading, Neutwo shoulder per channel above 1.0 only. Brighter than SDR above the pivot.
// HDR 2 (Psycho): Hable replaced by PsychoV30 (like Pharaoh's ACES). Scene-linear with
//        SDR's mid grey pinned to 0.18, LUT in a bounded proxy, then PsychoV30.
// HDR modes assume gamma 2.2 (in-game 50%).
//
// cb0 (traced): [0] screen w/h (uint), luma-in-alpha flag, brightness
//               [1] gamma 2.2, white mult 1, exposure mult 4, shadow boost 2
//               [2] 11, half screen w/h, vignette power 3
//               [3] vignette radius 299.52, vignette strength 0.04   [4].w LUT strength
// HDR 3 (PsychoV31): as HDR 2 with PsychoV31.

#include "./shared.h"
#include "./psycho_test30.hlsli"
#include "./psycho_test31.hlsli"
#include "../twattila/psycho35.hlsli"
#include "./shaders/common.hlsl"

// Hable, textbook constants: A .15, B .50, C .10, D .20, E .02, F .30.
#define HABLE_COEFFS 0.15f, 0.5f, 0.1f, 0.2f, 0.02f, 0.3f


// Extended user grading (from Pharaoh). Identity at defaults.
float3 ApplyExtendedUserGrading(float3 color)
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


// Extended peak: above 1.0 each channel rolls off into the headroom on its own, as SDR clips each channel: bright fire
// turns yellow / white at its core (as Ryse fix32). (The brightest-channel roll-off kept fire's full orange saturation up
// to Peak: flat, salmon.) At or below 1.0 untouched.
float3 ApplyExtendedPeakShoulder(float3 color)
{
  const float headroom = max(HDR_PEAK, 1.0001f) - 1.f;
  const float3 over = max(color - 1.f, 0.f);
  return min(color, 1.f)
         + float3(renodx::tonemap::Neutwo(over.r, headroom), renodx::tonemap::Neutwo(over.g, headroom),
                  renodx::tonemap::Neutwo(over.b, headroom));
}

// PsychoV31, on the scene-linear handoff (mid gray at 0.18); target-only where its source cage returns
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

float4 ResolveMainColor(float2 v1)
{
  static const float3 luma_weights = float3(0.212599993f, 0.715200007f, 0.0722000003f);
  // Threshold the float setting so a stray value cannot produce a hybrid mode.
  const int mode = (HDR < 0.5f) ? 0 : ((HDR < 1.5f) ? 1 : ((HDR < 2.5f) ? 2 : ((HDR < 3.5f) ? 3 : 4)));

  const float2 screen_size = (float2)asuint(cb0[0].xy);
  const int2 pixel = (int2)(uint2)(v1.xy * screen_size);

#if RMC_TONEMAP
  const int hdr_mode = mode;
  // Scene buffer is stored gamma-encoded.
  float3 color = pow(max(t0.Load(int3(pixel, 0)).xyz, 0.f), 2.2f);
  float y = dot(luma_weights, color);

  // t1 = auto exposure (0x6A1D2178): x exposure, y exp(-w/11), w adapted bright level.
  float4 auto_exposure = float4(t1[0].val[0], t1[0].val[1], t1[0].val[2], t1[0].val[3]);

  float3 sdr_linear = 0.f;
  float3 hdr_color = 0.f;
  if (y >= 9.99999975e-005f) {
    float white_point = max(cb0[1].y * auto_exposure.x, auto_exposure.w);
    float white_scale = 1.f / ApplyCurve(white_point, HABLE_COEFFS);

    // Pixels below the adapted bright level get up to (1 + cb0[1].w * ...) more exposure.
    float shadow_boost = cb0[1].w * (exp(-min(max(1.f, y), auto_exposure.w) / cb0[2].x) - auto_exposure.y) + 1.f;
    shadow_boost = (1.f < auto_exposure.w) ? shadow_boost : 1.f;
    float exposure = cb0[1].z * (shadow_boost * auto_exposure.x);

    float sdr_y = ApplyCurve(exposure * y, HABLE_COEFFS) * white_scale;
    sdr_linear = color * (sdr_y / y);

    if (hdr_mode == 1) {
      // Musa's default pivot.
      float coeffs[6] = { HABLE_COEFFS };
      Uncharted2::Config::Uncharted2ExtendedConfig uc2_config =
          Uncharted2::Config::CreateUncharted2ExtendedConfig(coeffs, white_scale);
      hdr_color = color * (Uncharted2::ApplyExtended(exposure * y, sdr_y, uc2_config) / y);
    } else if (hdr_mode >= 2) {
      // Hable dropped. Scene-linear handoff, scaled so the exposed value stock maps to
      // 0.18 (SDR mid grey) arrives at 0.18. Also samples the LUT where SDR does there.
      float mid_grey_input = InverseUncharted2(0.18f, white_point, HABLE_COEFFS);
      const float handoff_scale = exposure * 0.18f / max(mid_grey_input, 0.0001f);
      hdr_color = color * handoff_scale;
      // PsychoV35: Hue Clip Emulation toward the hue the stock Hable on the luminance and its per-channel clip give
      // (twattila/psycho35.hlsli), before the LUT as there.
      [branch]
      if (hdr_mode == 4 && SI.hue_clip > 0.f) {
        hdr_color = renodx::tonemap::psychov::HueClip(hdr_color, sdr_y / (y * handoff_scale), SI.hue_clip);
      }
    }
  }
#else
  // The game's tone mapping off: the scene is already display-referred (stock clamp), SDR in every mode.
  const int hdr_mode = 0;
  const float3 scene = saturate(t0.Load(int3(pixel, 0)).xyz);
#endif

  // Vignette (stock): elliptical, darkens only beyond the inscribed ellipse. Tint: a horizontal gradient and colour.
  float3 keep = 1.f;
#if RMC_VIGNETTE
  float2 centred = v1.xy * 2.f - 1.f;
  float2 dir = abs(centred) * screen_size;
  dir *= rsqrt(dot(dir, dir));
  float2 dir_scaled = cb0[2].zy * dir;
  float edge_radius = (cb0[2].y * cb0[2].z) / sqrt(dot(dir_scaled, dir_scaled));
  float centre_distance = length((float2)(asuint(cb0[0].xy) >> 1) * abs(centred));
  float beyond_edge = centre_distance - edge_radius;
  float vignette = min(1.f, cb0[3].y * pow(max(beyond_edge, 0.f) / cb0[3].x, cb0[2].w));
  if (mode != 0) vignette = saturate(vignette * SI.vignette);  // slider, HDR only
  float vignette_keep = 1.f - ((0.f < beyond_edge) ? vignette : 0.f);
#if RMC_TINT
  keep = cb0[4].xyz * min(vignette_keep, pow(v1.x * (1.f - cb0[3].z) + cb0[3].z, cb0[3].w));
#else
  keep = vignette_keep;
#endif
#endif

  if (hdr_mode == 0)
  {
#if RMC_TONEMAP
    float3 sdr = min(1.f, pow(sdr_linear, 1.f / cb0[1].x));
#else
    float3 sdr = scene;
#endif
#if RMC_LUT
    float3 lut = t2.Sample(s5_s, sdr).xyz;
    sdr = cb0[4].w * (lut - sdr) + sdr;
#endif
    sdr = cb0[0].w * sdr;
    sdr = sdr * keep;

    // 8-bit dither (stock): triangular inside (1/255, 254/255), rectangular at the ends.
    float noise = t3.Load(int3((pixel + int2(35, 27)) & 63, 0)).x;
    float triangular = ((noise < 0.5f) ? 1.f : -1.f) * sqrt(1.f - abs(noise * 2.f - 1.f)) + ((noise < 0.5f) ? -1.f : 1.f);
    triangular *= 0.00392156886f;
    float rectangular = noise * 0.00392156886f - 0.00196078443f;
    bool3 in_range = (0.00392156886f < sdr) && (sdr < 0.996078432f);
    float3 dither = in_range ? triangular.xxx : rectangular.xxx;
    dither.y = -dither.y;
    float4 o0;
    o0.xyz = saturate(sdr + dither);
    o0.w = (cb0[0].z != 0) ? dot(luma_weights, o0.xyz) : 1.f;

    // Undo the game's gamma (the tone mapped image's; else the frame's 2.2), scene to paper white, encode for the 2.2
    // swapchain.
#if RMC_TONEMAP
    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz, cb0[1].x);
#else
    o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
#endif
    o0.xyz *= SI.diffuse_white_nits / SI.graphics_white_nits;
    o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
    return o0;
  }

#if RMC_TONEMAP
#if RMC_LUT
  // HDR: LUT on a bounded gamma-2.2 proxy, then undo the scale (Total War bridge).
  float proxy_scale = renodx::tonemap::neutwo::ComputeMaxChannelScale(hdr_color, 1.f, HDR_PEAK);
  float3 proxy = renodx::color::gamma::EncodeSafe(hdr_color * proxy_scale);
  float3 lut = t2.Sample(s5_s, saturate(proxy)).xyz;
  proxy = cb0[4].w * (lut - proxy) + proxy;
  float3 graded_shaped = renodx::color::gamma::DecodeSafe(proxy);
  float3 graded_hdr = renodx::math::DivideSafe(graded_shaped, proxy_scale.xxx, graded_shaped);
#else
  float3 graded_hdr = hdr_color;
#endif

  if (hdr_mode == 1) {
    graded_hdr = ApplyExtendedUserGrading(graded_hdr);
    graded_hdr = ApplyExtendedPeakShoulder(graded_hdr);
  } else if (hdr_mode == 2) {
    graded_hdr = ApplyPsychoV30(graded_hdr);
  } else if (hdr_mode == 3) {
    graded_hdr = ApplyPsychoV31(graded_hdr);
  } else {
    graded_hdr = ApplyPsychoV35(graded_hdr, HDR_PEAK, SI.swap_chain_encoding == 2.f);
  }

  float4 o0;
  o0.xyz = graded_hdr * (SI.diffuse_white_nits / SI.graphics_white_nits);
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);

  // Brightness/fade, vignette and tint act on the encoded image, as in stock. No dither in FP16.
  o0.xyz *= cb0[0].w * keep;
  o0.w = (cb0[0].z != 0) ? saturate(dot(luma_weights, saturate(o0.xyz))) : 1.f;
  return o0;
#else
  return 0.f;
#endif
}

#endif  // SRC_TWROMEREMASTERED_RESOLVE_HLSLI_
