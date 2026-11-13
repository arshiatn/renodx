/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's swaying copies left out (rt_sway.hlsli, dlaa_helper/rt_d3d12.hpp): rt_compact.cs_6_5.hlsl
// marked the clusters of the swaying instances it turned down (their arguments' last word, zero in any argument).
// MODE_MASKS, MODE_FIRSTS, then MODE_KEPT (before the CLAS builds): the unmarked clusters' arguments (the captures' too)
// packed into `written`, the instantiated ones from 0 and the built ones from `built_arguments`, counted where the builds
// read their counts; each cluster BLAS's first cluster stays (none goes empty). MODE_LISTS (after them): each kept CLAS's
// address into its cluster BLAS's list (from its run's first place), counted in that BLAS's arguments.
// All in the clusters' own order, frame after frame: slots counted as the threads happened to run changed the BVH's order,
// and a ray past MAX_CANDIDATES depends on which blades it met first (identical input, some pixels changed each frame).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_sway.h -Vn RT_SWAY.

ByteAddressBuffer arguments : register(t8);  // MODE_KEPT: the clusters' arguments; MODE_LISTS: the kept ones
RWByteAddressBuffer written : register(u6);  // MODE_KEPT: the kept arguments; MODE_LISTS: the cluster BLASes' lists
// The cluster BLASes' arguments (16 bytes each: count, 0, list address), the CLAS counts (built, instantiated), each cluster
// BLAS's first cluster's place among the kept; from GROUPS 16 bytes per 64 clusters: which are kept (two uints), the kept
// before them (instantiated, built).
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
static const uint MODE_MASKS = 2u;
static const uint MODE_FIRSTS = 3u;  // a group of its own
static const uint CLAS_COUNTS = 64u;
static const uint RUN_PLACES = 72u;
static const uint GROUPS = 96u;

// MODE_FIRSTS: the kept clusters (instantiated, built) of each thread's run of groups.
groupshared uint2 run_kept[64];

// The lowest `bits` of two uints.
uint2 BitsBelow(uint bits) {
  return uint2((bits >= 32u) ? ~0u : (1u << bits) - 1u, (bits <= 32u) ? 0u : ((bits >= 64u) ? ~0u : (1u << (bits - 32u)) - 1u));
}

// MODE_FIRSTS: the kept clusters (instantiated, built) of the group of 64 from cluster `first` (MODE_MASKS' bits).
uint2 GroupKept(uint first) {
  const uint2 kept = counts.Load2(GROUPS + (first / 64u) * 16u);
  const uint2 first_ones = kept & BitsBelow(clamp(templated_clusters, first, first + 64u) - first);
  const uint instantiated_kept = countbits(first_ones.x) + countbits(first_ones.y);
  return uint2(instantiated_kept, countbits(kept.x) + countbits(kept.y) - instantiated_kept);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint index = id.x;
  // Its one group's 64 threads, every one at the barrier: a run of consecutive groups each, counted, then each group given the
  // kept before it, in the clusters' order (one thread walking them all cost 0.4-0.5 ms a frame more at 150 K clusters, 4K
  // harness).
  if (mode == MODE_FIRSTS) {
    const uint groups = (cluster_count + 63u) / 64u;
    const uint run = (groups + 63u) / 64u;
    const uint run_first = min(index * run, groups);
    const uint run_end = min(run_first + run, groups);
    uint2 kept = 0u;
    [loop]
    for (uint group = run_first; group < run_end; ++group) kept += GroupKept(group * 64u);
    run_kept[index] = kept;
    GroupMemoryBarrierWithGroupSync();
    uint2 before = 0u;
    [loop]
    for (uint earlier = 0u; earlier < index; ++earlier) before += run_kept[earlier];
    [loop]
    for (uint group = run_first; group < run_end; ++group) {
      counts.Store2(GROUPS + (group * 16u) + 8u, before);
      before += GroupKept(group * 64u);
    }
    if (index == 63u) counts.Store2(CLAS_COUNTS, before.yx);
    return;
  }
  if (index >= cluster_count) return;
  const bool instantiated = index < templated_clusters;
  const uint count_place = CLAS_COUNTS + (instantiated ? 4u : 0u);
  const uint group_first = index & ~63u;
  const uint group_place = GROUPS + (group_first / 64u) * 16u;
  // Of this group's clusters how many are instantiated (their run comes first).
  const uint group_instantiated = clamp(templated_clusters, group_first, group_first + 64u) - group_first;
  if (mode == MODE_MASKS) {
    if (index != group_first) return;
    uint2 kept = 0u;
    [loop]
    for (uint cluster = index; cluster < min(index + 64u, cluster_count); ++cluster) {
      const uint last_word = (cluster < templated_clusters) ? (cluster * 32u) + 28u : built_arguments + ((cluster - templated_clusters) * 64u) + 60u;
      const uint bit = cluster - index;
      if (arguments.Load(last_word) == 0u || cluster == 0u || cluster == grass_first || cluster == grass_end || cluster == units_end) {
        kept[bit / 32u] |= 1u << (bit % 32u);
      }
    }
    counts.Store2(group_place, kept);
    return;
  }
  if (mode == MODE_KEPT) {
    const uint bit = index - group_first;
    const uint2 kept = counts.Load2(group_place);
    if ((kept[bit / 32u] & (1u << (bit % 32u))) == 0u) return;
    const uint bytes = instantiated ? 32u : 64u;
    const uint from = instantiated ? index * 32u : built_arguments + ((index - templated_clusters) * 64u);
    // Its place among its kind: those kept before its group, then in it.
    const uint2 before_it = kept & BitsBelow(bit) & ~BitsBelow(instantiated ? 0u : group_instantiated);
    const uint2 before_group = counts.Load2(group_place + 8u);
    const uint slot = (instantiated ? before_group.x : before_group.y) + countbits(before_it.x) + countbits(before_it.y);
    const uint kept_place = instantiated ? slot : counts.Load(CLAS_COUNTS + 4u) + slot;
    const uint4 run_firsts = uint4(0u, grass_first, grass_end, units_end);
    [unroll]
    for (uint blas = 0u; blas < 4u; ++blas) {
      if (index == run_firsts[blas]) counts.Store(RUN_PLACES + (blas * 4u), kept_place);
    }
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
  // Its place in the run: the kept clusters keep their order, a run together.
  const uint kept_place = instantiated ? slot : counts.Load(CLAS_COUNTS + 4u) + slot;
  counts.InterlockedAdd(blas * 16u, 1u);
  written.Store2((run_first + kept_place - counts.Load(RUN_PLACES + (blas * 4u))) * 8u, addresses.Load2(address * 8u));
}
