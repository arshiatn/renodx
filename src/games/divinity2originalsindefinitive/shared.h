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
};

#define SI shader_injection
#define DOS2_PEAK_NITS clamp(SI.peak_white_nits, 1.f, 10000.f)
#define HDR_PEAK (DOS2_PEAK_NITS / max(SI.diffuse_white_nits, 0.0001f))
#define DOS2_CORRECT_HDR (SI.tonemap_type != 0.f )  //FIXED ACES OR TWEAKED ACES OR PSYCHO 

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
