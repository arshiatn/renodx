/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's campaign plants and forests (the game's rigidcampaignvegetation vs40_main, 0x84797031) with their world
// position and UV for ray tracing's stream output (rt_core.hpp): Attila's capture with the game's own size (POSITION2.x)
// and place (POSITION1). Standing and still: the game leans each plant by the camera's height (none with the camera at or
// below it) and sways it in the wind; with either its shadow would swing with the view.
#include "../twattila/rt_capture.hlsli"

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : COLOR0,
  float4 v3 : POSITION1,
  float v4 : POSITION2,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0)
{
  const float3 local = v0.xyz * v4;
  float turn_sin, turn_cos;
  sincos(v3.w, turn_sin, turn_cos);
  const float3 world = float3(local.x * turn_cos + local.z * turn_sin, local.y, local.z * turn_cos - local.x * turn_sin) + v3.xyz;
  world_uv = RtWorldUv(world, float2(v0.w, v1.w));
  position = 0.f;
}
