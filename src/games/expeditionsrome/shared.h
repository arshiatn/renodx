#ifndef SRC_EXPEDITIONSROME_SHARED_H_
#define SRC_EXPEDITIONSROME_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB

  float hdr;  // 0 SDR, 1 HDR (Extended: UE Filmic Extended), 2 HDR (PsychoV31)
  float bloom;
  float vignette;
  float sharpening;

  // PsychoV31
  float exposure;
  float highlights;
  float shadows;
  float contrast;  // x PsychoV31's own (1)
  float saturation;
  float highlight_saturation;
  float roll_off;      // 0 latest, 1 default, 2 soonest
  float game_shadows;  // 0 PsychoV31's, 1 the game's toe below mid gray

  float godrays;
  float volumetric_fog;  // multiplier (experimental)

  // Extended (PsychoV31 grading before the curve; all 1: skipped)
  float ext_exposure;
  float ext_highlights;
  float ext_shadows;
  float ext_contrast;
  float ext_saturation;
  float ext_highlight_saturation;

  float film_grain;            // tonemapper: grain intensity and jitter
  float chromatic_aberration;  // tonemapper: scene color fringe
  float ssao_mode;             // Ambient occlusion: 0 the game's, 1 GT-VBAO (talesofarise/ue_bitmask_ao.hlsli)
  float ssao_radius;           // GT-VBAO: metres
  float ssao_strength;         // GT-VBAO: exponent of the visibility, 1 GT-VBAO's own
  float ssao_thickness;        // GT-VBAO: metres behind each depth sample (more far away)
  float ssao_directions;       // GT-VBAO: 2^this directions around the view
  float ssao_quality;          // GT-VBAO: 8 x 2^this samples per side of a direction
  float ssao_frame;            // GT-VBAO: its noise's frame, 0-63

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
};

#define SI  shader_injection
#define HDR SI.hdr

// Peak relative to Paper White.
#define EXR_PEAK (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 1.f))
// Paper White / UI as a gain on gamma 2.2 encoded values (the proxy decodes 2.2 x UI nits).
#define EXR_ENCODED_SCALE pow(max(SI.diffuse_white_nits, 1.f) / max(SI.graphics_white_nits, 1.f), 1.f / 2.2f)

#ifndef __cplusplus
#if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
#elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
cbuffer shader_injection : register(b13) {
#endif
  ShaderInjectData shader_injection : packoffset(c0);
}

#include "../../shaders/renodx.hlsl"

// Volumetric fog density x `strength`: transmittance^strength, in-scatter by the same
// Beer-Lambert ratio (1 - T^s) / (1 - T). 1 is exact; 0 removes it.
float4 ScaleVolumetricFog(float4 fog, float strength) {
  if (strength == 1.f) return fog;
  if (strength <= 0.f) return float4(0.f, 0.f, 0.f, 1.f);
  const float transmittance = saturate(fog.a);
  const float scaled = pow(transmittance, strength);
  const float ratio = (1.f - transmittance > 1e-4f) ? (1.f - scaled) / (1.f - transmittance) : strength;
  return float4(fog.rgb * ratio, scaled);
}

#endif

#endif  // SRC_EXPEDITIONSROME_SHARED_H_
