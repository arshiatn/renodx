#ifndef SRC_TEMPLATE_SHARED_H_
#define SRC_TEMPLATE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float gamma_correction;
  // float custom_brightness;
  float peak_test;
  float swap_chain_encoding;
  // float whiteclip;

  // float dof;
  float uvdistort;
  float hdr;
  float bloom;
  float lensflare;
  float vignette;
  float godrays;
  float filmgrain;
  float chromaticaberration;
  float sharpening;
  // float lut;
  // float ui;

  //for ToneMapASS
  float exposure_tpm;
  float highlights_tpm;
  float shadows_tpm;
  float contrast_tpm;
  float saturation_tpm;
  
  //FOR PSYCHOV30:
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float saturation;
  float cone_response_exponent;

  //TODO: ONLY FOR DEBUG DELETE LATER:
  // float current_adaptive_state_bt709;
  // float current_background_state_bt709;

  // HDR (Extended) highlight expansion, 0 = off, 1 = on.
  float highlight_expansion;
  
  //Fix the shitty 7bit sun texture that gives me bending
  float sky_deband;

  //Game specific:
  float someindicators;

  // VFX Controls //////////////////////////////////////////////////////////////////////////////////
  // float vfxrain;
  // float vfxbloodsplash;
  float vfxbasebrightness;
  // float vfxweather;
  // float vfxsnow;
  float vfxfirebrightness;
  // float vfxnormalstrength;
  // float vfxshadowstrength;
  // float vfxlightingstrength;
  // float vfxspecularbrightness;
  // float vfxreflectionbrightness;
  // float vfxsoftparticlestrength;
  // float vfxopacity;
  // float vfxoverallbrightness;
  // float vfxfogamount;
  // float vfxfogbrightness;
  // float vfxdebugslice;
  // float vfxdebugmode;

  // Auto-exposure controls are appended so every existing injection offset
  // remains unchanged.
  float auto_exposure_highlight_protection;     // 0: stock, 1: protected
  float auto_exposure_highlight_headroom_stops; // 1 - 4 seems to work fine
};

#define SI shader_injection
#define HDR_PEAK max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 0.0001f) //added guard
#define HDR_INTSCALING SI.diffuse_white_nits / SI.graphics_white_nits
#define HDR_STOPS log2(HDR_MAXEXPECTED / HDR_PEAK)

// PsychoV30 keeps a fixed 1.0 exposure, 0.18 input/output anchors, full target
// gamut projection, BT.2020 target volume, and automatic compression. The
// compatibility-only parameters between purity and cone response stay neutral.
#define ApplyPsychoV30(color)                                               \
  renodx_custom::tonemap::psycho30::psychotm_test30(                             \
      (color), HDR_PEAK,  SI.exposure, SI.highlights, SI.shadows, SI.contrast,            \
      SI.saturation, 1.f, 100.f, 1.f, 1.f, 0, SI.cone_response_exponent, \
      0.18, 0.18) 


// // For the modded auto exposure
#define AutoExposureHighlightProtection     SI.auto_exposure_highlight_protection
#define AutoExposureHighlightHeadroomStops  SI.auto_exposure_highlight_headroom_stops

// FOR VFX AND EXTRA STUFF
#define HDR                       SI.hdr
// #define SOMEINDICATORS            SI.someindicators
#define VfxBaseBrightness         SI.vfxbasebrightness
// #define VfxWeatherBrightness      SI.vfxweather
// #define VfxSnowBrightness         SI.vfxsnow
// #define VfxBloodSplashBrightness  SI.vfxbloodsplash
// #define VfxRain                   SI.vfxrain

#define VfxFireBrightness         SI.vfxfirebrightness
// #define VfxNormalStrength         SI.vfxnormalstrength
// #define VfxShadowStrength         SI.vfxshadowstrength
// #define VfxLightingStrength       SI.vfxlightingstrength
// #define VfxSpecularBrightness     SI.vfxspecularbrightness
// #define VfxReflectionBrightness   SI.vfxreflectionbrightness
// #define VfxSoftParticleStrength   SI.vfxsoftparticlestrength
// #define VfxOpacity                SI.vfxopacity
// #define VfxOverallBrightness      SI.vfxoverallbrightness
// #define VfxFogAmount              SI.vfxfogamount
// #define VfxFogBrightness          SI.vfxfogbrightness
// #define VfxDebugDiffuseSlice SI.vfxdebugslice
// #define VfxDebugDiffuseSliceMode SI.vfxdebugmode
// #define ExposureMode SI.exposuremode
// #define VfxOpacity                1.0f
// #define VfxOverallBrightness      1.0f
// #define VfxFireBrightness         1.0f
// #define VfxNormalStrength         1.0f
// #define VfxShadowStrength         1.0f
// #define VfxLightingStrength       1.0f
// #define VfxSpecularBrightness     1.0f
// #define VfxReflectionBrightness   1.0f
// #define VfxSoftParticleStrength   1.0f
// #define VfxFogAmount              1.0f
// #define VfxFogBrightness          1.0f



#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_TEMPLATE_SHARED_H_
