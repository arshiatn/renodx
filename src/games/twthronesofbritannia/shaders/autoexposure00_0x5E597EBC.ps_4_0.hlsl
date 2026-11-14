/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// hdr_tone_mapper.hlsl ps_downsample_quarter_size_luma_stats (decompiled by hand from the game's 21 instructions): the luma
// statistics' chain, minimum / maximum / average in xyz, four samples a texel, down to 1 x 1. This game reads that last texel
// back to the CPU and computes the black and white points there, with the same formula as Attila's GPU pass (00016F65,
// never drawn here; empire.retail.dll +0x18041bf reads the stats' x, y, z, the 0.86 and 1.23 terms). Modded Auto Exposure
// (Attila's, autoexposure00): the maximum the exposure meters, limited relative to the frame's average, in that last texel
// (its input 2 x 2), so the white point the CPU makes follows Attila's. The picture's own brightness is untouched; off: the
// game's output.

cbuffer camera_VS_PS : register(b0) {
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
}

cbuffer render_target_dims_buffer : register(b1) {
  float2 g_render_target_recip_size : packoffset(c0);
}

SamplerState g_luma_stats_texture_sampler_s : register(s0);
Texture2D<float4> g_luma_stats_texture_sampler : register(t0);

#include "../shared.h"

void main(float4 v0 : SV_Position0, float2 v1 : TEXCOORD0, out float4 o0 : SV_Target0) {
  const float2 pixel = v0.xy + g_vpos_texel_offset;
  const float4 quarter = g_render_target_recip_size.xyxy * float4(0.25f, 0.25f, 0.25f, -0.25f);
  const float4 corners = pixel.xyxy * g_render_target_recip_size.xyxy + quarter;
  const float4 a = g_luma_stats_texture_sampler.SampleLevel(
      g_luma_stats_texture_sampler_s,
      pixel * g_render_target_recip_size + g_render_target_recip_size * float2(-0.25f, 0.25f), 0);
  const float4 b = g_luma_stats_texture_sampler.SampleLevel(
      g_luma_stats_texture_sampler_s, pixel * g_render_target_recip_size - quarter.xy, 0);
  const float4 c = g_luma_stats_texture_sampler.SampleLevel(g_luma_stats_texture_sampler_s, corners.xy, 0);
  const float4 d = g_luma_stats_texture_sampler.SampleLevel(g_luma_stats_texture_sampler_s, corners.zw, 0);
  o0.z = (b.z + (a.z + (c.z + d.z))) * 0.25f;
  o0.x = min(min(min(b.x, a.x), d.x), c.x);
  o0.yw = max(max(max(b.yw, a.yw), d.yw), c.yw);

  uint width, height;
  g_luma_stats_texture_sampler.GetDimensions(width, height);
  if (AutoExposureHighlightProtection > 0.f && width <= 2u && height <= 2u) {
    const float highlight_protection = saturate(AutoExposureHighlightProtection);
    const float highlight_headroom_stops = clamp(AutoExposureHighlightHeadroomStops, 0.f, 16.f);
    const float protected_max_luma = min(o0.y, max(o0.z, 0.000001f) * exp2(highlight_headroom_stops));
    o0.y = lerp(o0.y, protected_max_luma, highlight_protection);
  }
}
