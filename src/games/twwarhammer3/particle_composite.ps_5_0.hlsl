/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smooth Half-Size Particles (Experimental): the particles' composite (43606BB8, fx_vfx_composition_ps_mboit_nofilter) in the
// game's place while the row is on (vfx_particles.hpp), with the half-size layer smoothed (twattila/particle_history.hpp,
// t50-t51) and brought up by depth (twattila/particle_layer.hlsli: the half layer's depth t52, the scene's t53). As the game's:
// the colour sums of both layers and the 3D UI's, x (1 - T) / their alpha, T from the zeroth moment, the full-size textures
// read at the pixel's top-left corner. The game's moments hold the half layer's by nearest (A759AD5D, t54: the game's half
// zeroth moment): that comes off here for the smoothed one. o1 (the 3D UI) as the game's.
#include "./light_bounce.hlsli"
#include "../twattila/particle_layer.hlsli"

SamplerState g_sam_linear : register(s0);
Texture2D<float4> g_tex_fullres_zeroth_moment : register(t0);
Texture2D<float4> g_tex_game_vfx_fullres_transmittance_colour : register(t1);
Texture2D<float4> g_tex_ui3d_fullres_transmittance_colour : register(t3);
Texture2D<float4> g_tex_ui3d_halfres_transmittance_colour : register(t4);
Texture2D<float4> t_smoothed : register(t50);
Texture2D<float4> t_smoothed_extra : register(t51);  // x its optical depth
Texture2D<float> t_half_depth : register(t52);
Texture2D<float> t_depth : register(t53);
Texture2D<float> t_half_optical_depth : register(t54);

void main(float4 v0 : SV_Position0, float4 v1 : COLOR0, float2 v2 : TEXCOORD0, out float4 o0 : SV_Target0,
          out float4 o1 : SV_Target1) {
  const float2 uv = trunc(v0.xy) * g_screen_size.zw;
  const float4 ui = g_tex_ui3d_fullres_transmittance_colour.SampleLevel(g_sam_linear, uv, 0)
                    + g_tex_ui3d_halfres_transmittance_colour.SampleLevel(g_sam_linear, uv, 0);
  const int2 pixel = int2(v0.xy);
  float2 half_size;
  t_smoothed.GetDimensions(half_size.x, half_size.y);
  int2 base;
  const float distance = length(BounceViewPositionAt(pixel, t_depth.Load(int3(pixel, 0))));
  const float4 weights = ParticleUpsample(t_half_depth, uv * half_size - 0.5f, distance, base);
  const float4 colour = g_tex_game_vfx_fullres_transmittance_colour.SampleLevel(g_sam_linear, uv, 0)
                        + ParticleGather(t_smoothed, base, weights);
  // The game's half zeroth moment as the full one's bilinear read at the corner holds it: its 2 x 2 pixels' nearest texels.
  float added = 0.f;
  [unroll]
  for (int tap = 0; tap < 4; ++tap) {
    added += t_half_optical_depth.Load(int3(max(pixel - 1 + int2(tap & 1, tap >> 1), 0) >> 1, 0));
  }
  const float optical_depth = g_tex_fullres_zeroth_moment.SampleLevel(g_sam_linear, uv, 0).x - 0.25f * added
                              + ParticleGather(t_smoothed_extra, base, weights).x;
  const float transmittance = exp2(-1.442695f * optical_depth);
  const float scale = (1.f - transmittance) / (ui.w + colour.w);
  // As the game's: an infinite or undefined scale (no alpha) is 0.
  const float finite_scale = ((asuint(scale) & 0x7F800000u) != 0x7F800000u) ? scale : 0.f;
  o0 = float4(finite_scale * colour.rgb, transmittance);
  o1 = float4(finite_scale * ui.rgb, colour.w);
}
