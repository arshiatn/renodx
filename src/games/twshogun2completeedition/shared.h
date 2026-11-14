#ifndef SRC_TWSHOGUN2COMPLETEEDITION_SHARED_H_
#define SRC_TWSHOGUN2COMPLETEEDITION_SHARED_H_

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
  float highlight_contrast;          // above mid gray, 1 V31's own
  float shadow_contrast;             // below mid gray, 1 V31's own
  float flare;                       // 0 none, deeper blacks above it
  float dechroma;                    // 0 none, bright colors toward white above it
  float gamut_compression;           // 0 none (the display clips), 1 full
  float gamut_compression_mode;      // V31's target: 0 BT.709, 1 BT.2020, 3 Display P3
  float highlight_saturation_shift;  // saturation toward the game's SDR, as the hue shift, 0 - 1

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
  // sss.hlsli: Bend's parameters (DLSS addon SSSThickness, SSSContrast, SSSEdgeThreshold, SSSPrecisionOffset; 0 the game's).
  float sss_thickness;
  float sss_contrast;
  float sss_edge_threshold;
  float sss_precision_offset;
  float sss_foliage_light;  // sss.hlsli: Light Through Foliage, Bend's CasterLight (foliage_light.hpp; SSSFoliageLight, 0 off)
  // tonemap01: DLSS's jitter this frame (render pixels; the DLSS addon's, dlss_jitter.hpp), set before the draw
  float render_jitter_x;
  float render_jitter_y;

  // Render scale (DLSS addon): the rendered fraction of the full-size targets until the tonemap (addon, from the 3D
  // passes' viewports; 1: full size).
  float render_scale_x;
  float render_scale_y;
  // The DLSS addon's Ambient Occlusion (ReShade.ini [Shogun 2 DLSS]).
  float ssao_mode;       // 0 the game's ambient occlusion, 1 visibility bitmask
  float ssao_strength;   // visibility bitmask: an exponent
  float ssao_radius;     // visibility bitmask: x 2 m
  float ssao_thickness;  // visibility bitmask: metres behind each depth sample (more far away)

  // Appended.
  float ssao_quality;     // visibility bitmask: 8 x 2^this samples per side of a slice
  float ssao_directions;  // visibility bitmask: 2 x 2^this directions around the view
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
  float sun_shadow_softness;  // sss.hlsli: contact-hardening sun shadows (pcss.hlsli), the sun's size in degrees; 0 the game's
  float shadow_noise_frame;   // sss.hlsli: their samples' turn this frame (0-63)

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
  // sss.hlsli: Bend's samples and modes as Attila's rows (the DLSS addon's SSSHardSamples, SSSFadeSamples, SSSIgnoreEdges,
  // SSSAlongRay, SSSMirrorEdges): the counts 0 the game's (8, 16), the modes 0 the game's (off).
  float sss_hard_samples;
  float sss_fade_samples;
  float sss_ignore_edges;
  float sss_along_ray;
  float sss_mirror_edges;
  // waterplane00 (rivers and lakes in battles): Attila's Water Clarity (the water's depth before its deep colour / this; 0 or
  // 1 the game's) and Water Caustics (the bed's light x 1 + caustics x this; 0 off). The DLSS addon's WaterClarity / WaterCaustics.
  float water_clarity;
  float water_caustics;
  // autoexposure00: Attila's Modded Auto Exposure (its keys): 1 each metered luma at most the last frame's exposure's luma x
  // 2^stops (isolated highlights weigh less; the picture's brightness untouched), 0 the game's.
  float auto_exposure_highlight_protection;
  float auto_exposure_highlight_headroom_stops;
  float indicator_brightness;  // indicator00-02 in HDR: Attila's Unit/Building Indicator Brightness (some_indicators), 1 the game's
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

#endif  // SRC_TWSHOGUN2COMPLETEEDITION_SHARED_H_
