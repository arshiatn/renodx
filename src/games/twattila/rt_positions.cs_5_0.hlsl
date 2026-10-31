/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_scene.hpp): each pixel's world position from the scene's depth and the camera it was drawn with
// (its jittered inverse view-projection), and its distance from the camera (0: the sky, the depth's cleared value), for
// the helper's trace; and the camera's position in the sun texture (texel 1). On the campaign map a pixel below its sea is the
// sea floor: its rays start on the water above it (its forward pass takes their shadows, and the floor's tile edges shaded
// themselves: a thin stepped line over the open sea, debug view, 2026-10-31).
Texture2D<float> depth : register(t0);
RWTexture2D<float4> positions : register(u0);
RWTexture2D<float4> sun : register(u1);

cbuffer rt_positions : register(b0)
{
  float4 inverse_view_projection[4];  // camera_VS_PS's registers: rows acting on column vectors
  float4 camera;                      // xyz the camera's position
  uint4 size;                         // xy the render size, z the campaign map (its sea's level: y = 0)
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (all(id.xy == 0u)) sun[uint2(1, 0)] = float4(camera.xyz, 1.f);
  if (any(id.xy >= size.xy)) return;
  const float z = depth[id.xy];
  if (z <= 0.f || z >= 1.f) {
    positions[id.xy] = 0.f;
    return;
  }
  const float2 ndc = (float2(id.xy) + 0.5f) / float2(size.xy) * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float4 clip = float4(ndc, z, 1.f);
  const float4 world = float4(dot(inverse_view_projection[0], clip), dot(inverse_view_projection[1], clip),
                              dot(inverse_view_projection[2], clip), dot(inverse_view_projection[3], clip));
  float3 position = world.xyz / world.w;
  if (size.z != 0u && position.y < 0.f && camera.y > 0.f) position = lerp(camera.xyz, position, camera.y / (camera.y - position.y));
  positions[id.xy] = float4(position, length(position - camera.xyz));
}
