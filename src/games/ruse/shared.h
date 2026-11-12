#ifndef SRC_RUSE_SHARED_H_
#define SRC_RUSE_SHARED_H_

// Multiple of 4 floats: DX9 reads it as float4 registers (c200-c209, see SM3 below).
struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;

  float hdr;  // 0 SDR, 1 PsychoV31 Native Tone, 2 PsychoV31 Full Replacement, 3 PsychoV35 (Full Replacement).
  float bloom;
  float vignette;

  // Reuse padding without shifting existing injection offsets.
  float motion_blur;
  float depth_of_field;
  float reserved_2;
  float reserved_3;
  float reserved_4;

  // PsychoV31 controls.
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float saturation;
  float cone_response_exponent;

  float ui_scale;  // CPU-computed gamma 2.2 gain, only used by final UI draws.
  float ui_draw;   // 1 when the current draw targets the backbuffer, otherwise 0.

  float hue_shift;  // 1 bisector, 2 cone-response hue; shared by both Psycho modes.
  float video_scale;  // CPU-computed gamma 2.2 Paper White gain for final video draws.
  float reserved_5;
  float reserved_6;

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
  float reserved_7;
  float reserved_8;
  float reserved_9;
};

#define SI shader_injection
#define RUSE_REFERENCE_WHITE 203.f
#define HDR_PEAK (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 0.0001f))

// Native Tone uses 0.18 -> 0.18. Full Replacement supplies the same decoded native
// neutral-ramp anchor as the supplied V31 mode. Preserve customtest31 defaults:
// compression 1.5, no flare, BT.709 source cage, BT.2020 target. Contrast x cone = cone power.
// Hue weights: 1 bisector, 2 response hue; the default remains 2.
#define RUSE_V31_HIGHLIGHT_SOURCE_WEIGHT saturate((2.f - SI.hue_shift) / max(SI.hue_shift, 0.0001f))
#define ApplyPsychoV31Reference(color, anchor_in)                                                  \
  renodx::tonemap::psychov::custom_psychotm_test31(                                                \
      (color), HDR_PEAK, SI.exposure, SI.highlights, SI.shadows,                                   \
      SI.contrast * SI.cone_response_exponent, 0.f, 1.f, 1.f, SI.saturation, 1.f, 0.f,             \
      (anchor_in), 0.18f, 0.f, 1.f, renodx::tonemap::psychov::CUSTOM_PSYCHO31_TARGET_GAMUT_BT2020, \
      1.5f, 1.f, 0.5f * (1.f + RUSE_V31_HIGHLIGHT_SOURCE_WEIGHT), RUSE_V31_HIGHLIGHT_SOURCE_WEIGHT,  \
      renodx::tonemap::psychov::PSYCHO30_SOURCE_BOUNDARY_BT709, 1.f)

#define HDR SI.hdr

#ifndef __cplusplus
#if (__SHADER_TARGET_MAJOR == 3)
// DX9 (the game's shaders): no constant buffers. The addon writes the struct into
// c200-c209 (constant_buffer_offset = 200 * 4). High registers: the game uses the low ones.
float4 shader_injection_registers[10] : register(c200);
static const ShaderInjectData shader_injection = {
  shader_injection_registers[0], shader_injection_registers[1], shader_injection_registers[2],
  shader_injection_registers[3], shader_injection_registers[4], shader_injection_registers[5],
  shader_injection_registers[6], shader_injection_registers[7], shader_injection_registers[8],
  shader_injection_registers[9]
};
#elif ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
cbuffer shader_injection : register(b13, space50) {
  ShaderInjectData shader_injection : packoffset(c0);
}
#else
// DX11 (the swap chain proxy).
cbuffer shader_injection : register(b13) {
  ShaderInjectData shader_injection : packoffset(c0);
}
#endif

#include "../../shaders/renodx.hlsl"

#endif

#endif  // SRC_RUSE_SHARED_H_
