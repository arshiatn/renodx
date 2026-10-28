/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's scene data (dlaa_helper/rt_d3d12.hpp), after the request's vertices reach D3D12: per
// captured triangle its normal into the normal pool, a leaf's three UVs (the vertex texture's w: two halves) into the
// UV pool, and each capture's record (the first triangle's thread): where its UVs and normals are, its opacity mask's
// atlas slice and flags, the alpha threshold (the leaves' prepass shader: diffuse alpha from 0.5). Captures without a
// place (normal offset ~0) are left out. As Tales of Arise's 0x7A1E0D19.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_scene_data.h -Vn RT_SCENE_DATA.
#include "./rt_scene.hlsli"

Buffer<float4> vertex_positions : register(t7);  // the request's vertices (w: a leaf vertex's UV as two halves)
// Per capture: first vertex, vertex count, normal offset, UV offset; record, atlas slice | flags << 16, its first
// triangle in `triangle_count` (captures need not start at a multiple of three vertices), 0.
StructuredBuffer<uint4> capture_table : register(t8);
RWBuffer<uint4> records_written : register(u3);
RWBuffer<uint> uv_pool_written : register(u4);
RWBuffer<uint> normal_pool_written : register(u5);

cbuffer Dispatch : register(b1) {
  uint capture_count;
  uint triangle_count;
  uint dispatch_padding0;
  uint dispatch_padding1;
};

static const float LEAF_ALPHA_CUTOFF = 0.5f;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint triangle_index = id.x;
  if (triangle_index >= triangle_count) return;
  uint low = 0u;
  uint high = capture_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (capture_table[middle * 2u + 1u].z <= triangle_index) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 capture = capture_table[low * 2u];
  const uint4 placement = capture_table[low * 2u + 1u];
  const uint local = triangle_index - placement.z;
  if (capture.z == 0xFFFFFFFFu || local * 3u >= capture.y) return;
  const uint vertex = capture.x + local * 3u;
  const float4 a = vertex_positions[vertex];
  const float4 b = vertex_positions[vertex + 1u];
  const float4 c = vertex_positions[vertex + 2u];
  const float3 face = cross(b.xyz - a.xyz, c.xyz - a.xyz);
  const float face_length = length(face);
  normal_pool_written[capture.z + local] = (face_length > 0.f) ? OctahedralEncode(face / face_length) : OctahedralEncode(float3(0.f, 1.f, 0.f));
  const bool masked = ((placement.y >> 16) & RECORD_MASKED) != 0u;
  if (masked) {
    uv_pool_written[capture.w + local * 3u] = asuint(a.w);
    uv_pool_written[capture.w + local * 3u + 1u] = asuint(b.w);
    uv_pool_written[capture.w + local * 3u + 2u] = asuint(c.w);
  }
  if (local == 0u) {
    records_written[placement.x] = uint4(capture.w, capture.z, placement.y, masked ? asuint(LEAF_ALPHA_CUTOFF) : 0u);
  }
}
