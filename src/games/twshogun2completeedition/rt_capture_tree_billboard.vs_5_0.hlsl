/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's far trees (rigidtreebillboard vs30_main, 0xFAAE535B) for ray tracing's stream output (twattila/rt_core.hpp,
// Host::scene_draw's capture version: in clip space with the draw's camera, as the game's SV_Position, its UV at
// TEXCOORD1), as Attila's rt_capture_billboard: the card turned to the sun instead of the camera and showing the tree
// from there, so its shadow keeps its shape and does not swing with the view. The game's vertex shader picks one of the
// atlas's four views and a blend toward the next from the camera's way to the tree (its pixel shader 3053C54D reads view
// z at u - 0.25 z, the next at u - 0.25 (z + 1), the next where its dissolve map is under the blend): here the same
// choice from the sun's way to the tree, the nearer view.
// t0: rt_sun, its first texel the direction toward the sun.
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
}

Texture2D<float4> t_rt_sun : register(t0);

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float2 v2 : TEXCOORD0,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  out float4 position : SV_Position0,
  out float2 uv : TEXCOORD1)
{
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float2 from_tree = (across > 1e-3f) ? toward_sun.xz / across : float2(0.f, 1.f);

  // The game's card (its right vector from the view; here from the sun's direction), scaled by v5.w at v4.xyz.
  const float3 right = float3(from_tree.y, 0.f, -from_tree.x);
  const float3 world = (right * v0.x + float3(0.f, v0.y, 0.f)) * v5.w + v4.xyz;

  // The game's view (o3.zw) with the way from the sun to the tree for the camera's: the tree's facing v5.xz, its side.
  const float2 to_tree = -from_tree;
  const float2 facing = normalize(v5.xz);
  const float side = dot(to_tree, float2(facing.y, -facing.x));
  const float ahead = dot(to_tree, facing);
  float view = 0.f;
  float blend = -ahead;
  if (side < 0.f)
  {
    view = (ahead > 0.f) ? 2.f : 3.f;
    blend = (ahead > 0.f) ? ahead : -side;
  }
  else if (side > 0.f && ahead > 0.f)
  {
    view = 1.f;
    blend = side;
  }
  uv = float2(v2.x - 0.25f * (view + ((blend > 0.5f) ? 1.f : 0.f)), v2.y);

  // A card the game has faded out (v4.w 0: its pixel shader discards all of it): a NaN, which DXR skips.
  position = (v4.w > 0.f) ? mul(float4(world, 1.f), view_projection) : asfloat(0x7FC00000u).xxxx;
}
