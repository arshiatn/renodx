/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_SSS_HLSLI_
#define SRC_GAMES_TWATTILA_SSS_HLSLI_

// Screen-space shadows (screen_space_shadows.hpp): the settings of its compute shaders (sss_depth, sss_setup, sss_bend,
// sss_blur, sss_debug).
#define WAVE_SIZE 64

cbuffer sss_settings : register(b2)
{
  float2 g_sss_size;          // the depth's size (the screen's)
  float2 g_sss_inverse_size;
  float g_sss_thickness;      // Bend's SurfaceThickness
  float g_sss_samples;        // samples per pixel (the shadow's length in pixels)
  float g_sss_contrast;       // Bend's ShadowContrast
  float g_sss_edge_threshold; // Bend's BilinearThreshold
  float g_sss_hard_samples;   // Bend's HARD_SHADOW_SAMPLES
  float g_sss_fade_samples;   // Bend's FADE_OUT_SAMPLES
  float g_sss_ignore_edges;   // Bend's IgnoreEdgePixels
  float g_sss_precision;      // Bend's UsePrecisionOffset
  float g_sss_along_ray;      // Bend's BilinearSamplingOffsetMode
  float g_sss_softness;       // sss_blur: its taps this many pixels apart, 0 off
  float g_sss_debug_view;     // sss_debug: 1 the occlusion, 2 with the screen edge's limit
  float g_sss_mirror_edges;   // Bend's MirrorEdges (RenoDX)
  float g_sss_sun_direction;  // sss_setup: sun_direction's first float in lighting_VS_PS (Attila c0.y: 1)
  float2 g_sss_render_size;   // the 3D image's (DLSS: the depth's top-left corner; else the depth's size)
}

#endif  // SRC_GAMES_TWATTILA_SSS_HLSLI_
