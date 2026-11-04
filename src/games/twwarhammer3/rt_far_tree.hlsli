/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's far trees with their world position and UV for ray tracing's stream output (rt_core.hpp, SceneShader::sun_facing):
// the card (the game's math, 01830D95, CC8297ED) turned to the sun instead of the camera, showing the tree from there, so its shadow
// keeps its shape and does not swing with the view. t0: rt_sun, its first texel the direction toward the sun.
#include "../twattila/rt_capture.hlsli"

Texture2D<float4> t_rt_sun : register(t0);

cbuffer tree_billboard : register(b1) {
  float g_tree_displacement_radius;
  float3 g_billboard_cull_camera_position;
  float g_billboard_cull_distance_begin;
  float g_billboard_cull_distance_end;
  float g_tree_billboard_normal_noise;
  float g_num_billboard_faces;
};

float4 FarTreeWorldUv(float2 corner, float2 uv, float4 tree, float4 facing) {
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float2 from_tree = (across > 1e-3f) ? toward_sun.xz / across : float2(0.f, 1.f);
  const float3 right = float3(from_tree.y, 0.f, -from_tree.x);
  const float3 world = (right * corner.x + float3(0.f, corner.y, 0.f)) * facing.w + tree.xyz;

  // Which view shows the tree from there (the game's angle to the tree's facing), the nearest.
  const float2 facing_xz = normalize(facing.xz);
  const float side = facing_xz.x * from_tree.y - facing_xz.y * from_tree.x;
  const float angle = acos(clamp(dot(facing_xz, from_tree), -1.f, 1.f));
  const float view = g_num_billboard_faces * frac(((side < 0.f) ? angle : (6.28318548f - angle)) * 0.159154937f);
  const float2 card_uv = float2((uv.x + fmod(floor(view + 0.5f), g_num_billboard_faces)) / g_num_billboard_faces, uv.y);

  // A card the game has faded out (tree.w 0), or dissolves for the 3D tree there (its cull range, halfway): a NaN, which DXR skips.
  const bool shown = tree.w > 0.f
                     && distance(tree.xyz, g_billboard_cull_camera_position) > (g_billboard_cull_distance_begin + g_billboard_cull_distance_end) * 0.5f;
  return RtWorldUv(shown ? world : asfloat(0x7FC00000u).xxx, card_uv);
}
