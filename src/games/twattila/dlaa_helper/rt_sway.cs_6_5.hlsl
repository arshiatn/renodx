/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's swaying copies left out (rt_sway.hlsli, dlaa_helper/rt_d3d12.hpp): rt_compact.cs_6_5.hlsl
// marked the clusters of the swaying instances it turned down (their arguments' last word, zero in any argument).
// MODE_KEPT (before the CLAS builds): the unmarked clusters' arguments (the captures' too) packed into `written`, the
// instantiated ones from 0 and the built ones from `built_arguments`, counted where the builds read their counts; each
// cluster BLAS's first cluster stays (none goes empty). MODE_LISTS (after them): each kept CLAS's address into its
// cluster BLAS's list (from its run's first place), counted in that BLAS's arguments.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_sway.h -Vn RT_SWAY.

ByteAddressBuffer arguments : register(t8);  // MODE_KEPT: the clusters' arguments; MODE_LISTS: the kept ones
RWByteAddressBuffer written : register(u6);  // MODE_KEPT: the kept arguments; MODE_LISTS: the cluster BLASes' lists
// The cluster BLASes' arguments (16 bytes each: count, 0, list address), then the CLAS counts (built, instantiated).
RWByteAddressBuffer counts : register(u7);
RWByteAddressBuffer addresses : register(u8);  // MODE_LISTS: the CLASes' addresses (the instantiated ones' room first)

cbuffer Dispatch : register(b1) {
  uint mode;
  uint cluster_count;
  uint templated_clusters;  // the first, instantiated
  uint built_arguments;     // where the built ones' arguments start (bytes: the instantiated ones' room x 32)
  uint grass_first;         // the cluster BLASes' runs: the rest's, the grass's, the rest's, the smoke's
  uint grass_end;
  uint units_end;
  uint dispatch_padding;
};

static const uint MODE_KEPT = 0u;
static const uint MODE_LISTS = 1u;
static const uint CLAS_COUNTS = 64u;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint index = id.x;
  if (index >= cluster_count) return;
  const bool instantiated = index < templated_clusters;
  const uint count_place = CLAS_COUNTS + (instantiated ? 4u : 0u);
  if (mode == MODE_KEPT) {
    const uint bytes = instantiated ? 32u : 64u;
    const uint from = instantiated ? index * 32u : built_arguments + ((index - templated_clusters) * 64u);
    const bool run_first = index == 0u || index == grass_first || index == grass_end || index == units_end;
    if (arguments.Load(from + bytes - 4u) != 0u && !run_first) return;
    uint slot = 0u;
    counts.InterlockedAdd(count_place, 1u, slot);
    const uint to = instantiated ? slot * 32u : built_arguments + (slot * 64u);
    [unroll]
    for (uint part = 0u; part < 64u; part += 16u) {
      if (part >= bytes) break;
      // The mark cleared.
      written.Store4(to + part, arguments.Load4(from + part) & uint4(~0u, ~0u, ~0u, (part + 16u == bytes) ? 0u : ~0u));
    }
    return;
  }
  // MODE_LISTS: the kept argument here (the instantiated ones', then the built ones' from templated_clusters), its
  // cluster's BLAS by its id, its CLAS's address (the builds wrote them in the arguments' order).
  const uint slot = instantiated ? index : index - templated_clusters;
  if (slot >= counts.Load(count_place)) return;
  const uint cluster = arguments.Load(instantiated ? slot * 32u : built_arguments + (slot * 64u));
  const uint address = instantiated ? slot : (built_arguments / 32u) + slot;
  const uint blas = (cluster < grass_first) ? 0u : (cluster < grass_end) ? 1u : (cluster < units_end) ? 2u : 3u;
  const uint run_first = (blas == 0u) ? 0u : (blas == 1u) ? grass_first : (blas == 2u) ? grass_end : units_end;
  uint place = 0u;
  counts.InterlockedAdd(blas * 16u, 1u, place);
  written.Store2((run_first + place) * 8u, addresses.Load2(address * 8u));
}
