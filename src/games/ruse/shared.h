#ifndef SRC_RUSE_SHARED_H_
#define SRC_RUSE_SHARED_H_

// Multiple of 4 floats: DX9 reads it as float4 registers (see SM3 below).
struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;

  float hdr;  // 0 SDR, 1 Extended, 2 PsychoV30, 3 PsychoV30 (Direct, experimental)
  float bloom;
  float vignette;

  // For ToneMapASS (HDR Extended user grading)
  float exposure_tpm;
  float highlights_tpm;
  float shadows_tpm;
  float contrast_tpm;
  float saturation_tpm;

  // FOR PSYCHOV30:
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float saturation;
  float cone_response_exponent;

  float ui_scale;  // CPU-computed gamma 2.2 gain, only used by final UI draws.
  float ui_draw;   // 1 when the current draw targets the backbuffer, otherwise 0.
};

#define SI shader_injection
#define RUSE_REFERENCE_WHITE 203.f
#define HDR_PEAK (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 0.0001f))

// The existing mode uses 0.18 -> 0.18. Direct supplies a decoded native neutral-ramp
// input anchor. Both use the cone slider directly, BT.2020 projection and automatic compression.
// Compatibility-only parameters between purity and cone response stay neutral.
#define ApplyPsychoV30Reference(color, anchor_in, cone)                              \
  renodx_custom::tonemap::psycho30::psychotm_test30(                                \
      (color), HDR_PEAK, SI.exposure, SI.highlights, SI.shadows, SI.contrast,       \
      SI.saturation, 1.f, 100.f, 1.f, 1.f, 0, (cone), (anchor_in), 0.18f)

#define ApplyPsychoV30(color) ApplyPsychoV30Reference(color, 0.18f, SI.cone_response_exponent)

#define HDR SI.hdr

#ifndef __cplusplus
#if (__SHADER_TARGET_MAJOR == 3)
// DX9 (the game's shaders): no constant buffers. The addon writes the struct into
// c200-c204 (constant_buffer_offset = 200 * 4). High registers: the game uses the low ones.
float4 shader_injection_registers[5] : register(c200);
static const ShaderInjectData shader_injection = {
  shader_injection_registers[0], shader_injection_registers[1], shader_injection_registers[2],
  shader_injection_registers[3], shader_injection_registers[4]
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
