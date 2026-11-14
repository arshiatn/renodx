/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Troy's campaign tree billboards (campaigntreebillboards vertex_shader, 0xDF8EAF69) with their world position and UV for ray
// tracing's stream output (rt_core.hpp, SceneShader::sun_facing): the game's cards turned to the sun instead of the camera, as
// the far trees' (Pharaoh's rt_capture_far_tree), so their shadow keeps its shape. With the camera level with the tree and the
// sun behind it the game's own card is this one (no tilt toward the camera). As the game's, a card has no width closer than the
// game's start distance (the 3D tree, 54D2CFBD, stands there) or under the campaign's shroud (the game's t0 and s0, as bound).
// t1 (SceneShader::sun_slot): rt_sun, its first texel the direction toward the sun.
// RT_BILLBOARD_SCALE: the game's card scale (Pharaoh's twin CE5AA8D1: 0.47; its tilt toward the camera is 0 with the camera
// level with the tree).
#include "../twattila/rt_capture.hlsli"

#ifndef RT_BILLBOARD_SCALE
#define RT_BILLBOARD_SCALE 0.0109999999f
#endif

SamplerState sam_s : register(s0);
Texture2D<float4> tex_shroud : register(t0);
Texture2D<float4> t_rt_sun : register(t1);

cbuffer campaign_tree_billboard_params : register(b0) {
  float4 camera_pos_and_start_dist_squared : packoffset(c4);
  float4 shroud_remap : packoffset(c5);
};

void main(
  float3 v0 : POSITION0,
  float v1 : POSITION1,
  float v2 : POSITION2,
  float2 v3 : TEXCOORD0,
  float3 v4 : POSITION3,
  float2 v5 : POSITION4,
  float3 v6 : POSITION5,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0) {
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  // The game's direction from the camera to the tree, the sun's here.
  const float2 from_sun = (across > 1e-3f) ? -toward_sun.xz / across : float2(0.f, -1.f);

  float3 world = float3(v0.x * v5.x - v0.z * v5.y, v0.y, -v0.z * v5.x - v0.x * v5.y) * RT_BILLBOARD_SCALE + v4;

  // The game's start distance, each tree's own (1-2x by its place), and its shroud at the card's foot.
  const int2 cell = int2(float2(13.f, 10.f) * v4.xz);
  const float spread = 1.f + frac(0.01f * float(((cell.y ^ cell.x) * 0x07d3adeb) >> 12));
  const float3 from_camera = v4 - camera_pos_and_start_dist_squared.xyz;
  const float seen = (tex_shroud.SampleLevel(sam_s, shroud_remap.xy * world.xz + shroud_remap.zw, 0).w > 0.352941185f) ? 1.f : 0.f;
  const float width = (dot(from_camera, from_camera) * spread < camera_pos_and_start_dist_squared.w) ? 0.f : (v1 * seen) * RT_BILLBOARD_SCALE;

  world.xz += width * float2(from_sun.y, -from_sun.x);
  world_uv = RtWorldUv(world, float2(v3.x, 1.f - v3.y));
  position = 0.f;
}
