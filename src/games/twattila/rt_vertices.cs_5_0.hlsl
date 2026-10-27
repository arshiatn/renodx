/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_scene.hpp): the frame's captured triangles (stream output: world positions, three per triangle;
// a leaf's w its UV as two halves) into the vertex texture the helper copies its BLAS inputs from (dlaa_bridge.h
// RT_TRANSFER_WIDTH per row).
Buffer<float4> captured : register(t0);
RWTexture2D<float4> vertices : register(u0);

cbuffer rt_vertices : register(b0)
{
  uint4 rt_vertices;  // x the vertices, y the texture's width
}

[numthreads(256, 1, 1)]
void main(uint id : SV_DispatchThreadID)
{
  if (id >= rt_vertices.x) return;
  // Instances a shader collapses (scale 0, fades) give empty triangles; DXR skips those with a NaN x.
  const float4 position = captured[id];
  vertices[uint2(id % rt_vertices.y, id / rt_vertices.y)] = float4(all(isfinite(position.xyz)) ? position.xyz : asfloat(0x7FC00000u).xxx, position.w);
}
