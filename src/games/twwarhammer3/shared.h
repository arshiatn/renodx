#ifndef SRC_TEMPLATE_SHARED_H_
#define SRC_TEMPLATE_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float gamma_correction;
  // float custom_brightness;
  float peak_test;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB, 2 SDR (gamma 2.2; Peak, Paper White and UI unused)
  // float whiteclip;

  // float dof;
  float uvdistort;
  float hdr;
  float bloom;
  float vignette;
  // float godrays;
  // float filmgrain;
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


  //Game specific:
  // float someindicators;

  // VFX Controls //////////////////////////////////////////////////////////////////////////////////
  // float vfxrain;
  // float vfxbloodsplash;
  // float vfxbasebrightness;
  // float vfxweather;
  // float vfxsnow;
  // float vfxfirebrightness;
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
  // float auto_exposure_highlight_protection;     // 0: stock, 1: protected
  // float auto_exposure_highlight_headroom_stops; // 1 - 4 seems to work fine

  // Appended.
  float lensflare;
  float sharpening;
  float sky_deband;
  float render_scale_x;  // rendered fraction of the targets (addon, for the DOF)
  float render_scale_y;
  float dof;  // DOF strength (HDR)
  float bloom_remap_x;  // DLSS mod: bright pass gbuffer / particle mask scale (addon)
  float bloom_remap_y;
  float hue_shift;  // PsychoV30 highlight hue shift: 0 none, 1 Test30, 2 full

  // PsychoV31 (Rendering Mode 3), appended.
  float v31_exposure;
  float v31_highlights;
  float v31_shadows;
  float v31_contrast;
  float v31_highlight_contrast;          // above mid gray, 1 V31's own
  float v31_shadow_contrast;             // below mid gray, 1 V31's own
  float v31_flare;                       // 0 none, deeper blacks above it
  float v31_saturation;
  float v31_dechroma;                    // 0 none, bright colors toward white above it
  float v31_highlight_saturation;
  float v31_roll_off;                    // 0 latest, 1 default, 2 soonest
  float v31_gamut_compression;           // 0 none (the display clips), 1 full
  float v31_gamut_compression_mode;      // V31's target: 0 BT.709, 1 BT.2020, 3 Display P3
  float v31_highlight_hue_shift;         // hue toward the SDR image's above mid gray, 0 - 1
  float v31_highlight_saturation_shift;  // saturation toward the SDR image's, as the hue shift, 0 - 1
  // Appended (round 3).
  float video_autohdr;    // AutoHDR on the videos: 0 SDR, 1 video white at Peak
  float video_dithering;  // 1: 1 code value of dither before the video AutoHDR
  // Appended (round 6).
  float ssao_mode;        // 0 the game's ambient occlusion, 1 visibility bitmask
  float ssao_strength;    // visibility bitmask: an exponent
  float ssao_radius;      // visibility bitmask: x 2 m
  float ssao_thickness;   // visibility bitmask: metres behind each depth sample (more far away)

  // Appended.
  float ssao_quality;     // visibility bitmask: 8 x 2^this samples per side of a slice
  float ssao_directions;  // visibility bitmask: 2^this directions around the view
  float ssao_auto_thickness;  // visibility bitmask: 1 each occluder as thick as it is wide
  float ssao_resolution;  // visibility bitmask: 0 full, 1 half resolution (twwarhammer2/line_sweep_ao.hpp)
  float light_bounce_strength;    // Light Bounce (twattila/light_bounce.hpp): x the bounced light, 0 off
  float light_bounce_radius;      // Light Bounce: metres
  float light_bounce_quality;     // Light Bounce: 8 x 2^this samples per side of a direction
  float light_bounce_directions;  // Light Bounce: 2^this directions
  float light_bounce_resolution;  // Light Bounce: traced at 1 / 2^this of the resolution
  float light_bounce_temporal;    // Light Bounce: 1 the last frames kept
  float light_bounce_multi_bounce;  // Light Bounce: share of the last frame's bounce bounced again
  float light_bounce_checkerboard;  // Light Bounce: 1 half the texels traced each frame (temporal)
  float light_bounce_backface;  // Light Bounce: 1 back faces rejected (experiment)
  float light_bounce_auto_thickness;  // Light Bounce: 1 each occluder as thick as it is wide
  float light_bounce_denoise;  // Light Bounce: 1 the Denoiser
  float ssao_temporal;  // visibility bitmask: 1 Temporal Smoothing (twattila/ao_filter.hpp), its noise moving each frame
  float ssao_frame;     // its noise's frame, 0-63 (0: off)
  float ssao_edge_fix;  // Temporal Smoothing's Edge Fix (ao_filter.hpp)
  float ssao_denoise;   // the Denoiser (ao_filter.hpp)
  float fog_strength;   // battlefog00 / 01: x the fog's opacity and light (1: the game's)
  float tonemap_at_render_size;  // t00: 1 drawn at the render size, top-left (taa_dlss.hpp, below 100%), its overscan not used
  float sharpening_term;  // sharpening00: the game's part for its own upscale, taken off behind the addon's upscaler
  float shadow_map_scale;  // shadowcopy: the shadow maps' size against the game's 4096 when the addon made them (0.5, 0.25, 2), else 1
  float hue_clip;          // PsychoV35 (Rendering Mode 4): t00's Hue Clip Emulation, 0 - 1

  // Musa's PsychoV35 (psycho_test35.hlsli), its own sliders as V31's, appended.
  float v35_exposure;
  float v35_highlights;
  float v35_shadows;
  float v35_contrast;
  float v35_highlight_contrast;    // above mid gray, 1 V35's own
  float v35_shadow_contrast;       // below mid gray, 1 V35's own
  float v35_flare;                 // 0 none, deeper blacks above it
  float v35_saturation;
  float v35_dechroma;              // 0 none, bright colors toward white above it
  float v35_highlight_saturation;
  float v35_roll_off;              // 0 latest, 1 Musa's compression, 2 soonest
  float v35_gamut_compression;     // 0 none (the display clips), 1 full

  // Parity round (2026-11-14), appended.
  float someindicators;  // fx_indicator: x the colour (1: the game's bytes run)
};

#define SI shader_injection
// Rendering Mode SDR: what the game's encoded picture is decoded with. Gamma Correction on: its own gamma, so any in-game
// Gamma lands on the frame's 2.2; off: 2.2, what a 2.2 display shows of it, at the in-game Gamma.
#define SDR_STOCK_GAMMA ((SI.gamma_correction != 0.f) ? g_gamma_output : 2.2f)
// Output SDR has no Paper White (as Attila).
#define SDR_PAPER_WHITE ((SI.swap_chain_encoding == 2.f) ? 1.f : SI.diffuse_white_nits / SI.graphics_white_nits)
#define AO_NOISE_FRAME SI.ssao_frame  // twwarhammer2/bitmask_ao.hlsli
#define HDR_PEAK max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 0.0001f) //added guard
#define HDR_INTSCALING SI.diffuse_white_nits / SI.graphics_white_nits
#define HDR_STOPS log2(HDR_MAXEXPECTED / HDR_PEAK)

// PsychoV30 keeps a fixed 1.0 exposure, 0.18 input/output anchors, full target
// gamut projection, BT.2020 target volume, and automatic compression. The
// compatibility-only parameters between purity and cone response stay neutral.
// Last: highlight hue shift (SI.hue_shift).
#define ApplyPsychoV30(color)                                               \
  renodx_custom::tonemap::psycho30::psychotm_test30(                             \
      (color), HDR_PEAK,  SI.exposure, SI.highlights, SI.shadows, SI.contrast,            \
      SI.saturation, 1.f, 100.f, 1.f, 1.f, 0, SI.cone_response_exponent,       \
      0.18f, 0.18f, 1.f, 1, 1.f, 0.f, SI.hue_shift)


// // For the modded auto exposure
// #define AutoExposureHighlightProtection     SI.auto_exposure_highlight_protection
// #define AutoExposureHighlightHeadroomStops  SI.auto_exposure_highlight_headroom_stops

// FOR VFX AND EXTRA STUFF
#define HDR                       SI.hdr
// #define SOMEINDICATORS            SI.someindicators
// #define VfxBaseBrightness         SI.vfxbasebrightness
// #define VfxWeatherBrightness      SI.vfxweather
// #define VfxSnowBrightness         SI.vfxsnow
// #define VfxBloodSplashBrightness  SI.vfxbloodsplash
// #define VfxRain                   SI.vfxrain

// #define VfxFireBrightness         SI.vfxfirebrightness
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
cbuffer shader_injection : register(b10, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b10) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

// SDR output (Output: SDR, displays without HDR): Peak, Paper White and UI white are all the display's white, so every
// tone mapper and paper white scale lands in SDR; the proxy encodes gamma 2.2.
ShaderInjectData OutputShaderInjection(ShaderInjectData data) {
  [flatten]
  if (data.swap_chain_encoding == 2.f) {
    data.peak_white_nits = 100.f;
    data.diffuse_white_nits = 100.f;
    data.graphics_white_nits = 100.f;
  }
  return data;
}
#undef SI
#define SI OutputShaderInjection(shader_injection)

#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_TEMPLATE_SHARED_H_
