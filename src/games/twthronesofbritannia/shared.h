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
  float godrays;
  float filmgrain;
  float lensflare;
  // float lut;
  // float ui;
  
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
  float hue_shift;  // PsychoV30 highlight hue shift: 0 none, 1 Test30, 2 full

  // PsychoV31 (Rendering Mode 2), appended.
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
  float ssao_mode;        // 0 the game's ambient occlusion, 1 visibility bitmask
  float ssao_strength;    // visibility bitmask: x the game's strength
  float ssao_radius;      // visibility bitmask: x the game's radius
  float ssao_thickness;   // visibility bitmask: metres behind each depth sample (more far away)
  float ssr_quality;      // x the SSR's ray samples (stencil 1: shiny surfaces such as armour)

  // Appended.
  float ssao_quality;           // visibility bitmask: 8 x 2^this samples per side
  float light_bounce_strength;  // Light Bounce (twattila/light_bounce.hpp): x the bounced light, 0 off
  float light_bounce_radius;    // Light Bounce: metres
  float ssao_directions;        // visibility bitmask: 2^this directions around the view
  float ssao_auto_thickness;  // visibility bitmask: 1 each occluder as thick as it is wide (experiment)
  float ssao_denoise;  // visibility bitmask: 1 the Denoiser (twattila/ao_pass.hpp)
  float ssao_scale;  // visibility bitmask: the game's size over its own (set from the Resolution each frame)
  float ssao_resolution;  // visibility bitmask: 0 full, 1 half resolution (twattila/ao_pass.hpp)
  float light_bounce_quality;     // Light Bounce: 8 x 2^this samples per side of a direction
  float light_bounce_directions;  // Light Bounce: 2^this directions
  float light_bounce_resolution;  // Light Bounce: traced at 1 / 2^this of the resolution
  float light_bounce_temporal;    // Light Bounce: 1 the last frames kept
  float light_bounce_multi_bounce;  // Light Bounce: share of the last frame's bounce bounced again
  float light_bounce_checkerboard;  // Light Bounce: 1 half the texels traced each frame (temporal)
  float light_bounce_msaa_pixel;  // Light Bounce: 1 with the game's MSAA the apply once per pixel
  float light_bounce_backface;  // Light Bounce: 1 back faces rejected (experiment)
  float light_bounce_auto_thickness;  // Light Bounce: 1 each occluder as thick as it is wide
  float light_bounce_denoise;  // Light Bounce: 1 the Denoiser
  float ssao_temporal;            // visibility bitmask: 1 the last frames kept (twattila/ao_pass.hpp)
  float ssao_frame;               // its noise's frame, 0-63 (0: off)
  float ssao_edge_fix;            // visibility bitmask: 1 Temporal Smoothing's outlines from one history texel
  float ssr_hierarchical;         // ssr00 / ssr01: 1 the hierarchical march (twattila/ssr_hiz.hlsli), 0 the game's
  float ssr_rays;                 // the hierarchical march's rays: 0 every pixel, 1 half (checkerboard), 2 a quarter
  float ssr_frame;                // which pixels trace this frame (Half / Quarter), 0-3
  float sky_deband;               // the sky's debanding and dither (twattila/sky.hlsli): 0 off, 1-3 low / medium / high
  float effects_share;            // Effects at Output Resolution: DLSS's render share (1 off); ssr00 / ssr01's blur x it
  float ssr_traced;               // ssr_rays less the render scale's steps (Effects at Output Resolution), each frame
  float sky_smooth;               // the fog's and the sky's sky cube smoothed (twattila/sky_smooth.hlsli): 1 Low, 2 Medium, 3 High
  float sky_smooth_strength;      // how wide, x 0 to 2
  float fog_strength;             // battlefog00's opacity x this (1: the game's)

  // PsychoV35 (Rendering Mode 3): Musa's (twattila/psycho35.hlsli), its own sliders as V31's, appended.
  float hue_clip;                  // Hue Clip Emulation, 0 - 1
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

  float rt_ambient_alone;          // ssao00: 1 no ambient occlusion of its own (the ray-traced one replaces it), as Attila
  float grass_height_distance;     // grass vertex shaders: flat at 100 m x this (1: the game's), as Attila
  float dof;                       // the depth of field's blur size x this (hdr_to_screen t05-t08, t13-t16; 1: the game's), as Attila
};

#define SI shader_injection
#define HDR_PEAK max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 0.00009998806f) //added guard
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


// For the modded auto exposure
#define AutoExposureHighlightProtection     SI.auto_exposure_highlight_protection
#define AutoExposureHighlightHeadroomStops  SI.auto_exposure_highlight_headroom_stops

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
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
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
