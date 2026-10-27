/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_BITMASK_AO_HLSLI_
#define SRC_GAMES_TALESOFARISE_BITMASK_AO_HLSLI_

#include "./ue_bitmask_ao.hlsli"

// Ambient occlusion GT-VBAO (SI.ssao_mode 1; the DLSS addon's setting) in Tales' ambient occlusion pass (ao00-ao11), in
// place of the game's 16-sample spiral (ue_bitmask_ao.hlsli), on the pass's own inputs: the G-buffer normal (t0, world
// space, into view space through View.TranslatedWorldToView: cb1[8..10]), the pass's linear depth texture (cm) and its
// projection scale (cb0[28].x, x cb0[28].y vertically). Include after the pass's declarations (t0, s0_s, cb0, cb1).
float TalesBitmaskAmbientOcclusion(float2 screen_position, float2 normal_uv, uint2 pixel, Texture2D<float4> depth_texture,
                                   SamplerState depth_sampler, float2 depth_scale, float2 depth_offset) {
  const float3 world_normal = t0.SampleLevel(s0_s, normal_uv, 0).xyz * 2.f - 1.f;
  const float3 normal = world_normal.x * cb1[8].xyz + world_normal.y * cb1[9].xyz + world_normal.z * cb1[10].xyz;
  return UeBitmaskAmbientOcclusion(screen_position, normal, pixel, depth_texture, depth_sampler, depth_scale, depth_offset,
                                   float2(cb0[28].x, cb0[28].x * cb0[28].y), float4(1.f, 0.f, 0.f, -1e30f));
}

#endif  // SRC_GAMES_TALESOFARISE_BITMASK_AO_HLSLI_
