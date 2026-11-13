/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's compact draws (bridge v16, dlaa_helper/rt_d3d12.hpp): the addon streamed each draw's
// vertices once, in its cluster set's order (a block per instance). A group per cluster of a draw's instance: its first
// thread writes the cluster's arguments (RTX Mega Geometry; geometry index: the cluster's) and its record, a thread per
// triangle its normal and a leaf's UVs (rt_scene_data.hlsli). The first `templated_clusters` clusters (draws' whose set
// has templates) are instantiated from their template with their vertices in the vertex buffer (32 bytes of
// arguments each); the others are built from their triangles' 8-bit indices in the set's pool (64 bytes each, after
// `built_arguments` bytes). A swaying placed draw's instance SwayTraced turns down (rt_sway.hlsli), or grass's past
// GrassInRange: its clusters' arguments marked in their last word (rt_sway.cs_6_5.hlsl leaves them out), its triangles not
// written (but its first instance's: the set's micromap bake reads those UVs).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_compact.h -Vn RT_COMPACT.
#include "./rt_scene_data.hlsli"
#include "./rt_sway.hlsli"

// The cluster sets: each its clusters (first vertex in the block, vertices, first triangle, triangles), then its
// triangles (three bytes each: vertices of their cluster).
ByteAddressBuffer sets : register(t0);
// Four per draw: its first cluster (of the request's), first vertex, its set's place in the pool (bytes) and clusters;
// its block's vertices, its set's triangles, its normals' and UVs' offsets (NO_UVS: not alpha-tested); its record's atlas
// slice | flags << 16, its index for the garbage count, its bridge flags, its set's first template; a swaying placed
// draw's instances' rows (four texels each; NO_ROWS: none).
StructuredBuffer<uint4> draws : register(t8);
// NVAPI's INSTANTIATE_TEMPLATE_ARGS (32 bytes each), then from `built_arguments` its TRIANGLE_CLUSTER_ARGS (64 bytes each).
RWByteAddressBuffer cluster_args : register(u8);

cbuffer Dispatch : register(b1) {
  uint draw_count;
  uint cluster_count;
  uint first_record;  // the units' instance's InstanceID (a cluster's record: its geometry index past it)
  uint dispatch_padding;
  uint2 vertices_address;  // GPU addresses (low, high): the vertex buffer's, the pool's
  uint2 sets_address;
};

cbuffer Templates : register(b2) {
  uint2 templates_address;  // the templates' pool (low, high)
  uint template_bytes;      // a template's place
  uint templated_clusters;  // the first clusters, instantiated
  uint built_arguments;     // where the triangles' arguments start (bytes)
  uint sway_flags;          // rt_sway.hlsli
  float sway_range;
  float grass_range;        // Grass Ray Tracing Range (rt_sway.hlsli GrassInRange; 0: any)
  float4 sway_view[3];  // the view-projection's rows 0, 1 and 3
};

static const uint NO_ROWS = 0xFFFFFFFFu;
static const uint DRAW_GRASS = 64u;  // dlaa_bridge.h RT_GRASS, in a draw's bridge flags
static const uint INDEX_FORMAT_8BIT = 1u;  // NVAPI's cluster index format
static const uint GEOMETRY_NO_DUPLICATE_ANYHIT = 1u << 30;  // and geometry flags
static const uint GEOMETRY_OPAQUE = 1u << 31;

// `bytes` past the GPU address `base`.
uint2 Address(uint2 base, uint bytes) {
  const uint low = base.x + bytes;
  return uint2(low, base.y + ((low < base.x) ? 1u : 0u));
}

[numthreads(128, 1, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
  // Rows of 65535 groups (D3D12's most along x).
  const uint cluster = group.y * 65535u + group.x;
  if (cluster >= cluster_count) return;
  uint low = 0u;
  uint high = draw_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (draws[middle * 4u].x <= cluster) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 draw = draws[low * 4u];
  const uint4 block = draws[low * 4u + 1u];
  const uint4 kind = draws[low * 4u + 2u];
  const uint rows = draws[low * 4u + 3u].x;
  const uint local = cluster - draw.x;
  const uint instance = local / draw.w;
  // Its instance's origin (its rows' translation, as rt_place reads it).
  bool left_out = false;
  if ((sway_flags & SWAY_CUT) != 0u && rows != NO_ROWS) {
    const uint row = (rows + (instance * 4u)) * 16u;
    const float3 origin = asfloat(uint3(vertices_written.Load(row + 12u), vertices_written.Load(row + 28u), vertices_written.Load(row + 44u)));
    left_out = !SwayTraced(origin, sway_view[0], sway_view[1], sway_view[2], sway_range, sway_flags)
               || ((kind.z & DRAW_GRASS) != 0u && !GrassInRange(origin, sway_view[2], grass_range));
  }
  const uint4 desc = sets.Load4(draw.z + (local - instance * draw.w) * 16u);
  const uint first_vertex = draw.y + instance * block.x + desc.x;
  const uint first_triangle = instance * block.y + desc.z;  // of the draw's
  const uint corners = draw.z + draw.w * 16u + desc.z * 3u;
  const bool masked = ((kind.x >> 16) & RECORD_MASKED) != 0u;
  if (thread.x == 0u) {
    if (cluster < templated_clusters) {
      // Its cluster id and geometry index past the template's (0), the template, its vertices (16-byte stride).
      const uint arguments = cluster * 32u;
      cluster_args.Store4(arguments, uint4(cluster, cluster, Address(templates_address, (kind.w + local - instance * draw.w) * template_bytes)));
      cluster_args.Store4(arguments + 16u, uint4(Address(vertices_address, first_vertex * 16u), 16u, left_out ? 1u : 0u));
    } else {
      const uint arguments = built_arguments + (cluster - templated_clusters) * 64u;
      cluster_args.Store4(arguments, uint4(cluster, 0u, desc.w | (desc.y << 9) | (INDEX_FORMAT_8BIT << 24),
                                           cluster | (masked ? GEOMETRY_NO_DUPLICATE_ANYHIT : GEOMETRY_OPAQUE)));
      cluster_args.Store4(arguments + 16u, uint4(1u | (16u << 16), 0u, Address(sets_address, corners)));
      cluster_args.Store4(arguments + 32u, uint4(Address(vertices_address, first_vertex * 16u), 0u, 0u));
      cluster_args.Store4(arguments + 48u, uint4(0u, 0u, 0u, left_out ? 1u : 0u));
    }
    records_written[first_record + cluster] = uint4(masked ? block.w + first_triangle * 3u : NO_UVS, block.z + first_triangle, kind.x,
                                                    masked ? asuint(LEAF_ALPHA_CUTOFF) : 0u);
  }
  if ((left_out && instance != 0u) || thread.x >= desc.w) return;
  // Its three bytes, from the two words around them.
  const uint address = corners + thread.x * 3u;
  const uint2 words = sets.Load2(address & ~3u);
  const uint shift = (address & 3u) * 8u;
  const uint packed = (shift == 0u) ? words.x : ((words.x >> shift) | (words.y << (32u - shift)));
  const uint triangle_index = first_triangle + thread.x;
  WriteTriangle(first_vertex + uint3(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu), (kind.z & CAPTURE_TERRAIN) == 0u, kind.y,
                block.z + triangle_index, masked ? block.w + triangle_index * 3u : NO_UVS);
}
