/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila deformed motion (object_motion.hpp): a cloth, sail or rope draw drawn again as points (one per index, all on one
// pixel, motion_capture_*.vs) records its vertices' world positions from motion_draw.x (motion_draw.y of them), and its
// first point (its first index's vertex) the draw's anchor: by draw slot for this frame's motion draw, and in this
// frame's table for the next frame (motion.hlsli).
#include "./motion.hlsli"

RWBuffer<float4> motion_positions : register(u0);
RWBuffer<float4> motion_draw_anchors : register(u1);
RWBuffer<uint> motion_keys : register(u2);
RWBuffer<uint4> motion_placements : register(u3);

void main(float4 position : SV_Position, float4 world : TEXCOORD0, nointerpolation uint vertex : TEXCOORD1,
          uint point_index : SV_PrimitiveID)
{
  if (vertex < motion_draw.y) motion_positions[motion_draw.x + vertex] = world;
  if (point_index != 0u) return;
  motion_draw_anchors[motion_unit.w] = float4(world.xyz, 1.f);
  const uint key = ((motion_unit.x * 2654435761u) ^ (asuint(world.x) * 2246822519u) ^ (asuint(world.y) * 3266489917u)
                    ^ (asuint(world.z) * 668265263u)) | 1u;
  float4 rows[3] = {float4(1.f, 0.f, 0.f, world.x), float4(0.f, 1.f, 0.f, world.y), float4(0.f, 0.f, 1.f, world.z)};
  MotionRecord(motion_keys, motion_placements, key, world.xz, rows, uint4(motion_unit.x, motion_draw.x, motion_draw.y, vertex));
}
