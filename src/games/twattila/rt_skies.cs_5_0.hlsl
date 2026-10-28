/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_core.hpp): a new static capture that holds the camera and is larger than the view reaches, or is
// centred on it, is a sky (Attila's campaign map draws its dome as a rigid object around the camera, 900 units across;
// placed draws follow the camera with it): it casts no shadow,
// its triangles go empty in the vertex texture (a NaN x, which DXR skips). After rt_vertices, one group per capture:
// its vertices' bounds, then the test.
Buffer<float4> captured : register(t0);
RWTexture2D<float4> vertices : register(u0);

cbuffer rt_skies : register(b0)
{
  uint4 rt_skies;  // x the capture's first vertex, y its vertices, z 1: clip-space positions (the game's own shaders), w the texture's width
  float4 inverse_view_projection[4];  // their camera's (rows acting on column vectors)
  float4 camera;  // xyz its position, w the view's reach
}

static const uint GROUP_SIZE = 64u;

groupshared float3 lows[GROUP_SIZE];
groupshared float3 highs[GROUP_SIZE];

[numthreads(GROUP_SIZE, 1, 1)]
void main(uint thread : SV_GroupIndex)
{
  float3 low = 1e30f;
  float3 high = -1e30f;
  [loop]
  for (uint index = thread; index < rt_skies.y; index += GROUP_SIZE) {
    const float4 position = captured[rt_skies.x + index];
    float3 world = position.xyz;
    if (rt_skies.z != 0u) {
      const float4 projected = float4(dot(inverse_view_projection[0], position), dot(inverse_view_projection[1], position),
                                      dot(inverse_view_projection[2], position), dot(inverse_view_projection[3], position));
      world = projected.xyz / projected.w;
    }
    if (!all(isfinite(world))) continue;
    low = min(low, world);
    high = max(high, world);
  }
  lows[thread] = low;
  highs[thread] = high;
  GroupMemoryBarrierWithGroupSync();
  [unroll]
  for (uint step = GROUP_SIZE / 2u; step > 0u; step >>= 1) {
    if (thread < step) {
      lows[thread] = min(lows[thread], lows[thread + step]);
      highs[thread] = max(highs[thread], highs[thread + step]);
    }
    GroupMemoryBarrierWithGroupSync();
  }
  const float3 size = highs[0] - lows[0];
  // Centred on the camera across (a dome the game keeps around it, smaller than the view reaches: battles).
  const bool centred = all(abs((lows[0].xz + highs[0].xz) * 0.5f - camera.xz) <= 0.01f * max(size.x, size.z));
  if (any(camera.xyz < lows[0]) || any(camera.xyz > highs[0]) || (max(size.x, max(size.y, size.z)) <= camera.w && !centred)) return;
  [loop]
  for (uint emptied = thread; emptied < rt_skies.y; emptied += GROUP_SIZE) {
    const uint vertex = rt_skies.x + emptied;
    vertices[uint2(vertex % rt_skies.w, vertex / rt_skies.w)] = asfloat(0x7FC00000u).xxxx;
  }
}
