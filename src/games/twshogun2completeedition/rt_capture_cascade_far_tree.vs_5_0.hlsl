/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a far tree card's shadow (rigidtreebillboard vs30_shadow, 30C453EE: turned to the shadow camera, the sun)
// for the outside-view geometry shader: the game's corner in world space and the UV of its pixel shader's mask test (6190CA61's
// t1). The game picks one of the atlas's four views and a blend toward the next from the shadow camera's way to the tree (6190CA61
// reads view z at u - 0.25 z, the next at u - 0.25 (z + 1), the next where its dissolve map is under the blend): the nearer view
// here, as rt_capture_tree_billboard. A card the game has faded out (v4.w 0: its pixel shader discards all of it): a NaN, which
// the geometry shader collapses and DXR skips.
#include "./rt_cascade_leaf.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c2);
}

RtCascadeLeaf main(
  float4 v0 : POSITION0,
  float2 v2 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2)
{
  // The game's card: its right vector across the shadow camera's view, scaled by v5.w at v4.xyz.
  const float3 toward = -normalize(float3(view._m02, 0.f, view._m22));
  const float3 right = normalize(cross(float3(0.f, 1.f, 0.f), toward));
  const float3 world = v4.xyz + (v0.x * right * v5.w) + (v0.y * float3(0.f, 1.f, 0.f) * v5.w);

  // The game's view (its TEXCOORD1.zw) from the shadow camera's way to the tree and the tree's facing.
  const float3 to_tree = normalize(float3(v4.x, 0.f, v4.z) - float3(camera_position.x, 0.f, camera_position.z));
  const float3 facing = -normalize(float3(v5.x, 0.f, v5.z));
  const float ahead = dot(to_tree, facing);
  const float side = dot(to_tree, cross(facing, float3(0.f, 1.f, 0.f)));
  float atlas_view = 0.f;
  float blend = ahead;
  if ((ahead < 0.f) && (side > 0.f)) {
    atlas_view = 1.f;
    blend = side;
  } else if ((ahead < 0.f) && (side < 0.f)) {
    atlas_view = 2.f;
    blend = abs(ahead);
  } else if (side < 0.f) {
    atlas_view = 3.f;
    blend = abs(side);
  }

  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = float2(v2.x - 0.25f * (atlas_view + ((blend > 0.5f) ? 1.f : 0.f)), v2.y);
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = (v4.w > 0.f) ? float4(world, 1.f) : asfloat(0x7FC00000u).xxxx;
  return leaf;
}
