#ifndef SRC_TEMPLATE_SHARED_H_
#define SRC_TEMPLATE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float gamma_correction;
  float peak_test;
  float swap_chain_encoding;
  // float whiteclip;

  // float dof;
  // float uvdistort;
  float bloom;
  // float vignette;
  // float filmgrain;
  // float lut;
  // float ui;
  
  float tonemap_type;

  //FOR PSYCHOV30:
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float saturation;
  float cone_response_exponent;

  // Native Vanilla grading
  float aces_exposure;
  float aces_contrast;
  float aces_highlights;
  float aces_shadows;
  float aces_saturation;
  float aces_highlight_saturation;

  // Full RenoDX ACES grading
  float aces_tweaked_exposure;
  float aces_tweaked_contrast;
  float aces_tweaked_highlights;
  float aces_tweaked_shadows;
  float aces_tweaked_saturation;
  float aces_tweaked_highlight_saturation;

  // PsychoV30 fire colours. Retain the existing keys/slots for saved settings.
  float fire_color_strength;
  float fire_color_hue;

  // Independent Tweaked ACES fire colours.
  float aces_tweaked_fire_color_strength;
  float aces_tweaked_fire_color_hue;

  // Independent Vanilla fire colours, disabled by default.
  float vanilla_fire_color_strength;
  float vanilla_fire_color_hue;

  // PsychoV35 (Rendering Mode 4): Musa's (twattila/psycho35.hlsli) with its own sliders, appended.
  float hue_clip;                  // Hue Clip Emulation, 0 - 1
  float v35_exposure;
  float v35_highlights;
  float v35_shadows;
  float v35_contrast;
  float v35_highlight_contrast;    // above mid gray, 1 V35's own
  float v35_shadow_contrast;       // below mid gray, 1 V35's own
  float v35_flare;                 // 0 none, deeper blacks above it
  float v35_saturation;
  float v35_dechroma;              // 0 none, bright colours toward white above it
  float v35_highlight_saturation;
  float v35_roll_off;              // 0 latest, 1 Musa's compression, 2 soonest
  float v35_gamut_compression;     // 0 none (the display clips), 1 full
  float v35_mid_gray;              // 0 the game's HDR curve (15 nits at Paper White 203), 1 its SDR picture's

  // Vanilla: 1 each channel its own shoulder near Peak, 0 the brightest channel's for all (ratios kept).
  float vanilla_per_channel_shoulder;

  // Experimental, 0 = off: t01 samples a modded LUT at its texel centres (the game's addressing is half a texel off).
  float exact_lut_sampling;
};

#define SI shader_injection
#define DOS2_PEAK_NITS clamp(SI.peak_white_nits, 1.f, 10000.f)
#define HDR_PEAK (DOS2_PEAK_NITS / max(SI.diffuse_white_nits, 0.0001f))
#define DOS2_CORRECT_HDR (SI.tonemap_type != 0.f )  // Vanilla, Tweaked ACES, PsychoV30 or PsychoV35

//This is for t01/t00 otherwise contrast slider still makes some differences.
//Traced them via devkit in the cbuffer of t00 and t01.
// Contrast 0.85 - 1.00 stays at 1516.4874f but goes up to 4xxx something at 1.1
#define DOS2_HDR_LUT_SHAPER_DOMAIN float2(5.4931616e-6f, 1516.4874f)

// Modified HDR LUTs use an alpha tag; stock SDR/Off LUTs retain alpha 1.
// t01 reads the tag before choosing the corresponding shaper domain.
#define DOS2_HDR_LUT_ALPHA 0.f

// Neutral tone-stage reference from the captured native curve.
// Output is relative to the 203 scene-brightness reference, not to peak.
#define DOS2_GRAY_INPUT 0.18f
#define DOS2_GRAY_OUTPUT (15.f / 203.f)

// PsychoV35's slider defaults here: the game's own response (its ACES contrast around mid gray and toe below it, its
// colour at equal lightness), Highlight Contrast keeping PsychoV35's highlights. Fitted on WARP against the game's t00
// through its LUT and t01 (test/tw_psychov35 part 6, 10.); the other PsychoV35 sliders keep Musa's defaults.
#define DOS2_V35_CONTRAST 1.42f
#define DOS2_V35_SHADOWS 0.43f
#define DOS2_V35_SATURATION 0.93f
#define DOS2_V35_HIGHLIGHT_CONTRAST 0.87f

// Apply PsychoV30 from Test30.
#define ApplyPsychoV30(color)                                                       \
  renodx_custom::tonemap::psycho30::psychotm_test30(                                \
      (color), HDR_PEAK, SI.exposure, SI.highlights, SI.shadows, SI.contrast,        \
      SI.saturation, 1.f, 100.f, 1.f, 1.f, 0, SI.cone_response_exponent,             \
      float3(DOS2_GRAY_INPUT, DOS2_GRAY_INPUT, DOS2_GRAY_INPUT),                    \
      float3(DOS2_GRAY_OUTPUT, DOS2_GRAY_OUTPUT, DOS2_GRAY_OUTPUT))



#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b10, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b10) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_TEMPLATE_SHARED_H_
