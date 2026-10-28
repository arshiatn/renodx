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

cbuffer rt_vertices : register(b0)
{
  uint4 rt_vertices;  // x the vertices, y the texture's width, z 1: clip-space positions (the game's own shaders)
  float4 inverse_view_projection[4];  // their camera's (rows acting on column vectors)
}

[numthreads(256, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID)
{
  // Rows of 65535 groups (D3D11's most along x).
  const uint id = dispatch_id.y * 65535u * 256u + dispatch_id.x;
  if (id >= rt_vertices.x) return;
  // Instances a shader collapses (scale 0, fades) give empty triangles; DXR skips those with a NaN x.
  float4 position = captured[id];
  if (rt_vertices.z != 0u) {
    const float4 world = float4(dot(inverse_view_projection[0], position), dot(inverse_view_projection[1], position),
                                dot(inverse_view_projection[2], position), dot(inverse_view_projection[3], position));
    const float2 uv = captured_uv[id];
    position = float4(world.xyz / world.w, asfloat(f32tof16(uv.x) | (f32tof16(uv.y) << 16)));
  }
  vertices[uint2(id % rt_vertices.y, id / rt_vertices.y)] = float4(all(isfinite(position.xyz)) ? position.xyz : asfloat(0x7FC00000u).xxx, position.w);
}
