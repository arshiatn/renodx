/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a far tree card's shadow from the other card set (rigidtreebillboard_3k vs30_shadow, 63E273F4: not drawn
// in the data run's traces) for the outside-view geometry shader: the game's corner in world space (the card of 30C453EE's,
// rt_capture_cascade_far_tree, from TEXCOORD1-2) and the UV of its pixel shader's mask test (9D7E2040's t1). Its atlas has
// g_num_billboard_faces views in a row: the game picks one and a blend toward the next from the angle between the tree's facing
// and the shadow camera's way to it (9D7E2040 reads (u + view) / faces, the next where its dissolve map is under the blend):
// the nearer view here. A card the game has faded out (v2.w 0: its pixel shader discards all of it): a NaN.
#include "./rt_cascade_leaf.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c2);
}

cbuffer tree_billboard_constants : register(b1)
{
  float g_num_billboard_faces : packoffset(c1.w);
}

RtCascadeLeaf main(float4 v0 : POSITION0, float4 v1 : TEXCOORD0, float4 v2 : TEXCOORD1, float4 v3 : TEXCOORD2)
{
  // The game's card: its right vector across the shadow camera's view, scaled by v3.w at v2.xyz.
  const float3 toward = -normalize(float3(view._m02, 0.f, view._m22));
  const float3 right = normalize(cross(float3(0.f, 1.f, 0.f), toward));
  const float3 world = v2.xyz + (v0.x * right * v3.w) + (v0.y * float3(0.f, 1.f, 0.f) * v3.w);

  // The game's view (its TEXCOORD1.zw): the turn from the tree's facing to the camera's way, a share of a full turn x the faces.
  const float2 to_camera = normalize(camera_position.zx - v2.zx);
  const float2 facing = normalize(v3.xz);
  const float cosine = dot(facing.yx, to_camera);
  const float turn = ((facing.x * to_camera.x) - (facing.y * to_camera.y) < 0.f) ? acos(cosine) : (6.28318548f - acos(cosine));
  const float views = frac(0.159154937f * turn) * g_num_billboard_faces;
  const float nearer = fmod(floor(views) + ((frac(views) > 0.5f) ? 1.f : 0.f), g_num_billboard_faces);

  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = float2((v1.x + nearer) / g_num_billboard_faces, v1.y);
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = (v2.w > 0.f) ? float4(world, 1.f) : asfloat(0x7FC00000u).xxxx;
  return leaf;
}
