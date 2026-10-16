#ifndef SRC_TWSHOGUN2_SHARED_H_
#define SRC_TWSHOGUN2_SHARED_H_

struct ShaderInjectData {
  float peak_white_nits;
  float diffuse_white_nits;
  float graphics_white_nits;
  float swap_chain_encoding;  // 0 HDR10, 1 scRGB, 2 SDR (gamma 2.2; Peak, Paper White and UI unused)

  float hdr;  // 0 SDR, 1 HDR (Extended), 2 HDR (PsychoV31)
  float bloom;

  // PsychoV31
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float cone_response_exponent;  // x contrast: the cone response's power, without contrast's purity compensation
  float saturation;
  float highlight_saturation;
  float roll_off;  // 0 latest, 1 default, 2 soonest
  float highlight_hue_shift;  // hue toward the game's SDR above mid gray, 0 (V31's own) - 1

  // HDR (Extended) user grading: PsychoV31's grade before the game's curve (all at 1: skipped)
  float ext_exposure;
  float ext_highlights;
  float ext_shadows;
  float ext_contrast;
  float ext_saturation;
  float ext_highlight_saturation;
  float ext_highlight_hue_shift;  // hue toward the game's SDR above mid gray, 0 (the curve's own) - 1

  // Extra (HDR)
  float lens_flare;
  float vignette;
  float heat_haze;        // distortion offsets (tonemap01)
  float depth_of_field;   // blur radius (dofblur00 / 01 / 02)
  float battle_fog;       // fog density and haze (fog00 / 01)
  float sun;              // sun disc (sun00)
  float foliage_glints;   // the sun's specular on foliage (sunlight00-03)
  // VFX (HDR): brightness of the glowing particles and meshes (vfxglow00 / 01, glowmesh00 / 01), trails (ribbon00,
  // projectiletrail00), lights (particlelight00, pointlight00 / 01), smoke and dust (vfxoverlay00), and its opacity
  // (transmittance^this).
  float vfx_glow;
  float vfx_big_fire;
  float vfx_campfire;
  // vfxglow00 / 01, glowmesh00 / 01: this draw's (addon, by its effect: vfx_glow, vfx_big_fire or vfx_campfire)
  float vfx_glow_draw;
  float vfx_trails;
  float vfx_firelight;
  float vfx_smoke;
  float vfx_smoke_opacity;
  float vfx_smoke_draw;  // vfxoverlay00: 1 on the smoke buffer's draw (addon, from the blend state)
  float video_autohdr;    // video00: inverse tone mapping, 0 off
  float video_dithering;  // video00: 0 off, 1 on
  float ssr_resolution;   // ssr.hlsli, from the DLSS addon's overlay: 0 the game's (half the render size), 1 full
  float sss_samples;      // sss.hlsli: 120 x 2^this samples, 0 the game's (DLSS addon SSSSamples: 0-5)
  float sss_stability;    // sss.hlsli: 0 the game's, 1 contrast halved, 2 and edge pixels cast none (SSSStability)
  float shadow_map_stability;  // sss.hlsli: the cascades' normal offset, 0 none, 1 / 2 0.75 / 1.5 texels
  // tonemap01: DLSS's jitter this frame (render pixels; the DLSS addon's, dlss_jitter.hpp), set before the draw
  float render_jitter_x;
  float render_jitter_y;

  // Render scale (DLSS addon): the rendered fraction of the full-size targets until the tonemap (addon, from the 3D
  // passes' viewports; 1: full size).
  float render_scale_x;
  float render_scale_y;
};

#define SI  shader_injection
#define HDR SI.hdr

// SDR output (displays without HDR): the display's white is the game's and the UI's.
#define SHOGUN_SDR_OUTPUT (SI.swap_chain_encoding == 2.f)

// Peak relative to Paper White.
#define SHOGUN_PEAK \
  (SHOGUN_SDR_OUTPUT ? 1.f : (max(SI.peak_white_nits, SI.diffuse_white_nits) / max(SI.diffuse_white_nits, 1.f)))

// Paper White relative to UI (the UI target's white).
#define SHOGUN_PAPER_WHITE (SHOGUN_SDR_OUTPUT ? 1.f : (SI.diffuse_white_nits / max(SI.graphics_white_nits, 1.f)))

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

#endif  // SRC_TWSHOGUN2_SHARED_H_
