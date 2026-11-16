/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's scene data (dlaa_helper/rt_d3d12.hpp), after the request's vertices reach D3D12: per
// captured triangle its normal and a leaf's UVs (rt_scene_data.hlsli), and each capture's record (the first triangle's
// thread): where its UVs and normals are, its opacity mask's atlas slice and flags, the alpha threshold. Captures
// without a place (normal offset ~0) are left out. As Tales of Arise's 0x7A1E0D19. Grass Ray Tracing Range: grass
// captured as drawn (every frame: Rome II's, WH1's, WH2's) goes empty past it, a triangle at its first vertex (placed
// and compact grass: rt_compact.cs_6_5.hlsl).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_scene_data.h -Vn RT_SCENE_DATA.
#include "./rt_scene_data.hlsli"
#include "./rt_sway.hlsli"

static const uint CAPTURE_INSTANCED = 8u;  // dlaa_bridge.h RT_INSTANCED
static const uint CAPTURE_GRASS = 64u;     // RT_GRASS
static const uint CAPTURE_SUN_ONLY = 128u;  // RT_SUN_ONLY (sprite soldiers and far trees: RT_GRASS too)

// Per capture: first vertex, vertex count, normal offset, UV offset; record, atlas slice | flags << 16, its first
// triangle in `triangle_count` (captures need not start at a multiple of three vertices), its bridge flags.
StructuredBuffer<uint4> capture_table : register(t8);

cbuffer Dispatch : register(b1) {
  uint capture_count;
  uint triangle_count;
  float own_cutoff;  // CAPTURE_OWN_CUTOFF's alpha cut-off (dlaa_bridge.h rt_leaf_cutoff)
  float grass_range;  // Grass Ray Tracing Range (InViewRange; 0: any)
  float4 view_row3;   // the view-projection's row 3 (InViewRange)
};

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
  const bool masked = ((placement.y >> 16) & RECORD_MASKED) != 0u;
  const uint3 vertex = capture.x + local * 3u + uint3(0u, 1u, 2u);
  // A static capture stays as captured while the camera moves: only those captured every frame.
  if ((placement.w & (CAPTURE_DYNAMIC | CAPTURE_GRASS | CAPTURE_SUN_ONLY | CAPTURE_INSTANCED)) == (CAPTURE_DYNAMIC | CAPTURE_GRASS)
      && !InViewRange(asfloat(vertices_written.Load3(vertex.x * 16u)), view_row3, grass_range)) {
    const uint empty = 0x7FC00000u;
    [unroll]
    for (uint corner = 0u; corner < 3u; ++corner) {
      vertices_written.Store3(vertex[corner] * 16u, uint3(empty, empty, empty));
    }
  } else {
    WriteTriangle(vertex, (placement.w & (CAPTURE_DYNAMIC | CAPTURE_TERRAIN)) == CAPTURE_DYNAMIC, low, capture.z + local,
                  masked ? capture.w + local * 3u : NO_UVS);
  }
  if (local == 0u) {
    const float cutoff = ((placement.w & CAPTURE_OWN_CUTOFF) != 0u) ? own_cutoff : LEAF_ALPHA_CUTOFF;
    records_written[placement.x] = uint4(capture.w, capture.z, placement.y, masked ? asuint(cutoff) : 0u);
  }
}
