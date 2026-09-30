#ifndef SRC_TEMPLATE_SHARED_H_
#define SRC_TEMPLATE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;
  float tonemap_type; // 0 = SDR, 1 = UC2 Extended / PsychoV30, 2 = UC2 Extended / Pragmap.

  float vignette;
  float bloom;
  float godrays; 
  float ui; //visibility fo UI
  float firehighlightmultiplier; // Pragmap fire control.
  float candlehighlightmultiplier; // Pragmap candle control.
  float candlehighlightmultiplierpsycho;  
  float player_light_intensity;
  float local_light_intensity;

  //For PsychoV30
  float exposure;
  float highlights;
  float shadows;
  float cone_response_exponent;
  float contrast;
  float purity_scale;

  float brazier_boost_enabled; // PsychoV30: fixed RGB boost on/off.
};

#define SI shader_injection
// Keep HDR a binary flag for shared effects and output handling.
#define TONEMAP_SDR 0.f
#define TONEMAP_PSYCHO 1.f
#define TONEMAP_PRAGMAP 2.f
#define TONEMAP_MODE (SI.tonemap_type)
#define HDR (TONEMAP_MODE == TONEMAP_SDR ? 0.f : 1.f)
#define HDR_PEAK (SI.peak_white_nits / SI.diffuse_white_nits)
#define HDR_INTSCALING (SI.diffuse_white_nits / SI.graphics_white_nits)

// PsychoV30 receives linear BT.709, uses the exposed grading controls, and
// returns linear BT.709 (possibly signed for its BT.2020 target volume).
// Input/output anchors remain 0.18; full target projection and automatic
// compression use PsychoV30 defaults. Compatibility placeholders stay neutral.
#define ApplyAttilaPsychoV30(color)                                               \
  renodx_custom::tonemap::psycho30::psychotm_test30(                             \
      (color), HDR_PEAK, SI.exposure, SI.highlights, SI.shadows, SI.contrast,            \
      SI.purity_scale, 1.f, 100.f, 1.f, 1.f, 0, SI.cone_response_exponent)

//extra
#define GODRAYS SI.godrays
#define LOCAL_LIGHT_INTENSITY (HDR == 1.f ? SI.local_light_intensity : 1.f)
#define PLAYER_LIGHT_INTENSITY (HDR == 1.f ? SI.player_light_intensity : 1.f)

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b10, space50) { // Changed to b10
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b10) {          // Changed to b10
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_TEMPLATE_SHARED_H_
