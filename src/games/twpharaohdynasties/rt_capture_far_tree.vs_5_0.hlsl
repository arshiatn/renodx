/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Pharaoh's far trees (rigidtreebillboard vs40_main, 0x97C53219) with their world position and UV for ray tracing's stream
// output (rt_core.hpp, SceneShader::sun_facing): the game's card turned to the sun instead of the camera, showing the tree from
// there, so its shadow keeps its shape and does not swing with the view. With the camera level with the tree the game's own card
// is this one (no tilt, no push toward the camera). t0: rt_sun, its first texel the direction toward the sun.
// RT_FAR_TREE_NO_CONSTANTS (Warhammer II: its card vertex shaders bind no tree constants, b1 holds another draw's): every card
// the game draws, its view's face.
#include "../twattila/rt_capture.hlsli"

Texture2D<float4> t_rt_sun : register(t0);

cbuffer camera : register(b0) {
  float3 camera_position : packoffset(c0);
};

#ifdef RT_FAR_TREE_NO_CONSTANTS
static const float g_billboard_cull_distance_end = 0.f;
static const int g_single_face = 0;
#else
cbuffer tree_billboard_constants : register(b1) {
  float g_billboard_cull_distance_end : packoffset(c4.y);
  int g_single_face : packoffset(c5);
};
#endif

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float v4 : TEXCOORD3,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0) {
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float2 from_tree = (across > 1e-3f) ? toward_sun.xz / across : float2(0.f, 1.f);
  const float3 right = float3(from_tree.y, 0.f, -from_tree.x);
  const float3 world = (right * v0.x + float3(0.f, v0.y, 0.f)) * v3.w + v2.xyz;

  // The game's four views: its pixel shader blends the two around the angle to the tree's facing, the nearest here; or the
  // tree's one face.
  float face = 0.f;
  if (g_single_face != 0) {
    face = floor(frac(v2.x * 31.3131008f) * 4.f);
  } else {
    const float2 facing = normalize(v3.xz);
    const float side = facing.x * from_tree.y - facing.y * from_tree.x;
    const float angle = acos(clamp(dot(facing, from_tree), -1.f, 1.f));
    const float view = 4.f * frac(((side < 0.f) ? angle : (6.28318548f - angle)) * 0.159154937f);
    face = fmod(floor(view + 0.5f), 4.f);
  }
  const float2 uv = float2(v1.x + (0.25f * face), v1.y);

  // A card the game has faded out (v2.w 0: its dither discards all of it) or culls for the 3D tree there (its pixel shader
  // discards closer than the cull distance's end): a NaN, which DXR skips.
  const bool shown = v2.w > 0.f && distance(v2.xyz, camera_position) >= g_billboard_cull_distance_end;
  world_uv = RtWorldUv(shown ? world : asfloat(0x7FC00000u).xxx, uv);
  position = 0.f;
}
