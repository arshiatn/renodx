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
