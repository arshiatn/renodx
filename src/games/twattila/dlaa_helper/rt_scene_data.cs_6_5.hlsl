/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's scene data (dlaa_helper/rt_d3d12.hpp), after the request's vertices reach D3D12: per
// captured triangle its normal into the normal pool, a leaf's three UVs (the vertex texture's w: two halves) into the
// UV pool, and each capture's record (the first triangle's thread): where its UVs and normals are, its opacity mask's
// atlas slice and flags, the alpha threshold (the leaves' prepass shader: diffuse alpha from 0.5). Captures without a
// place (normal offset ~0) are left out. As Tales of Arise's 0x7A1E0D19.
// Garbage triangles go empty (NaN: left out of the BLASes) and are counted with the first capture that had one: a
// coordinate past MAX_COORDINATE, or an edge past a part's size (MAX_PART: a dynamic capture's but the terrain's; any
// other's MAX_EDGE). A vertex millions of metres out spread the scene's bounds until every ray met every instance
// (traces of a second, GPU resets, ATN 2026-10-28).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_scene_data.h -Vn RT_SCENE_DATA.
#include "./rt_scene.hlsli"

// The request's vertices, 16 bytes each (w: a leaf vertex's UV as two halves).
RWByteAddressBuffer vertices_written : register(u6);
// Triangles left out: far off, stretched parts, stretched others; the first such capture's index.
RWByteAddressBuffer left_out : register(u7);
// Per capture: first vertex, vertex count, normal offset, UV offset; record, atlas slice | flags << 16, its first
// triangle in `triangle_count` (captures need not start at a multiple of three vertices), its bridge flags.
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
static const uint CAPTURE_TERRAIN = 1u;  // dlaa_bridge.h RT_TERRAIN
static const uint CAPTURE_DYNAMIC = 2u;  // RT_DYNAMIC
static const float MAX_COORDINATE = 2.0e5f;  // metres (battle maps: a few kilometres)
static const float MAX_PART = 200.f;         // a unit's, grass's or cloth's triangle edge at most
static const float MAX_EDGE = 5000.f;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  // Rows of 65535 groups (D3D12's most along x).
  const uint triangle_index = id.y * 65535u * 64u + id.x;
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
  const float4 a = asfloat(vertices_written.Load4(vertex * 16u));
  const float4 b = asfloat(vertices_written.Load4(vertex * 16u + 16u));
  const float4 c = asfloat(vertices_written.Load4(vertex * 16u + 32u));
  const bool far_off = any(abs(a.xyz) > MAX_COORDINATE) || any(abs(b.xyz) > MAX_COORDINATE) || any(abs(c.xyz) > MAX_COORDINATE);
  const bool part = (placement.w & (CAPTURE_DYNAMIC | CAPTURE_TERRAIN)) == CAPTURE_DYNAMIC;
  const float longest = max(max(distance(a.xyz, b.xyz), distance(b.xyz, c.xyz)), distance(c.xyz, a.xyz));
  if (far_off || longest > (part ? MAX_PART : MAX_EDGE)) {
    const uint empty = 0x7FC00000u;
    vertices_written.Store4(vertex * 16u, uint4(empty, empty, empty, asuint(a.w)));
    vertices_written.Store4(vertex * 16u + 16u, uint4(empty, empty, empty, asuint(b.w)));
    vertices_written.Store4(vertex * 16u + 32u, uint4(empty, empty, empty, asuint(c.w)));
    left_out.InterlockedAdd(far_off ? 0u : (part ? 4u : 8u), 1u);
    left_out.InterlockedMin(12u, low);
  }
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
