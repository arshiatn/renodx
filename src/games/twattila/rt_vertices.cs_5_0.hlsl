/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_scene.hpp): the frame's captured triangles (stream output: world positions, three per triangle;
// a leaf's w its UV as two halves) into the vertex texture the helper copies its BLAS inputs from (dlaa_bridge.h
// RT_TRANSFER_WIDTH per row). The game's own shaders' captures are clip-space positions, their foliage's UV beside them.
Buffer<float4> captured : register(t0);
Buffer<float2> captured_uv : register(t1);
RWTexture2D<float4> vertices : register(u0);
#if defined(RT_VERTICES_UV_SCALES)
// rt_core Host::mask_uv_scales: the frame's masks read at the UV times a material register, by first vertex (first vertex,
// vertices, the register's components for u and v), and each one's register as its draw had it.
Buffer<uint4> uv_scale_ranges : register(t2);
Buffer<float4> uv_scales : register(t3);
#endif

cbuffer rt_vertices : register(b0)
{
  uint4 rt_vertices;  // x the vertices, y the texture's width, z 1: clip-space positions (the game's own shaders)
  float4 inverse_view_projection[4];  // their camera's (rows acting on column vectors)
#if defined(RT_VERTICES_WORLD_AFTER) || defined(RT_VERTICES_UV_SCALES)
  uint4 rt_vertices_world;  // x where the world-space part lies in the captured buffer; y the scaled masks (RT_VERTICES_UV_SCALES)
#endif
}

// Captured vertex `id`'s UV (a clip-space capture's foliage); with RT_VERTICES_UV_SCALES times its draw's register where it is
// a scaled mask's.
float2 CapturedUv(uint id)
{
  float2 uv = captured_uv[id];
#if defined(RT_VERTICES_UV_SCALES)
  uint low = 0u;
  uint high = rt_vertices_world.y;
  [loop]
  while (low < high) {
    const uint middle = (low + high) / 2u;
    if (uv_scale_ranges[middle].x <= id) {
      low = middle + 1u;
    } else {
      high = middle;
    }
  }
  if (low != 0u) {
    const uint4 range = uv_scale_ranges[low - 1u];
    const float4 scale = uv_scales[low - 1u];
    if (id - range.x < range.y) uv *= float2(scale[range.z], scale[range.w]);
  }
#endif
  return uv;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
  // Rows of 65535 groups (D3D11's most along x).
  const uint id = dispatch_id.y * 65535u * 256u + dispatch_id.x;
  if (id >= rt_vertices.x) return;
  // Instances a shader collapses (scale 0, fades) give empty triangles; DXR skips those with a NaN x.
#if defined(RT_VERTICES_WORLD_AFTER)
  float4 position = captured[id < rt_vertices.w ? id : (id - rt_vertices.w + rt_vertices_world.x)];
#else
  float4 position = captured[id];
#endif
#if defined(RT_VERTICES_WORLD_AFTER)
  // From w on: a world-space part (placed meshes and their rows).
  if (rt_vertices.z != 0u && id < rt_vertices.w) {
#else
  if (rt_vertices.z != 0u) {
#endif
#if defined(RT_VERTICES_RELATIVE)
    // rt_core Host::clip_relative: rows 0-2 the offset from the camera (row 3), no division by the w row (it cancels).
    const float3 offset = float3(dot(inverse_view_projection[0], position), dot(inverse_view_projection[1], position),
                                 dot(inverse_view_projection[2], position));
    const float2 uv = CapturedUv(id);
    position = float4(inverse_view_projection[3].xyz + offset, asfloat(f32tof16(uv.x) | (f32tof16(uv.y) << 16)));
#else
    const float4 world = float4(dot(inverse_view_projection[0], position), dot(inverse_view_projection[1], position),
                                dot(inverse_view_projection[2], position), dot(inverse_view_projection[3], position));
    const float2 uv = CapturedUv(id);
    position = float4(world.xyz / world.w, asfloat(f32tof16(uv.x) | (f32tof16(uv.y) << 16)));
#endif
  }
  vertices[uint2(id % rt_vertices.y, id / rt_vertices.y)] = float4(all(isfinite(position.xyz)) ? position.xyz : asfloat(0x7FC00000u).xxx, position.w);
}
