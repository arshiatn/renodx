/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's placed meshes in the partitioned TLAS (dlaa_helper/rt_d3d12.hpp, RENODX_TW_PTLAS only; B23):
// rt_place's descs of this request (`instances_written`, from `first_instance`) against the last dispatch's
// (`instances_before`, from its first instance), slot by slot. A slot k below this request's count whose PTLAS fields
// changed, or that is new, gets one WRITE_INSTANCE argument at place `placed_base + k`; one below the last count and past
// this one an inactive write. Three modes (`mode`): mark (each group of 64 slots: its changed slots' bits and their count),
// scan (one group: the groups' places, the operation's count) and write (each changed slot's argument). Two runs of one
// request write the same arguments in the same order (ours: rt_place's own rule, its lines 13-14; the A/A floor stays 0).
// As in NVIDIA's vk_partitioned_tlas sample (Apache-2.0; src/partitioned_tlas.cpp onRender, its update shader
// shaders/animation_update_instances.comp.glsl): the CPU writes the one WRITE_INSTANCE operation (type, argument address
// and stride) and the GPU its argument count and the arguments, from a test per instance on the GPU; that sample takes
// each argument's place from an atomicAdd on the count (an order the hardware picks), ours from a prefix sum (the fixed
// order above); its operation count is always 1, ours too (a spare inactive write when nothing changed: scan).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_ptlas.h -Vn RT_PTLAS.

ByteAddressBuffer instances_before : register(t0);  // the last dispatch's descs (D3D12_RAYTRACING_INSTANCE_DESC, 64 bytes each)
// The records' BLAS words (two uints each, rt_place.cs_6_5.hlsl's table; bit 6: its BLAS built this request, PTLAS only).
ByteAddressBuffer table : register(t8);
ByteAddressBuffer instances_written : register(t15);  // this request's (rt_place's output, read only here)
// The PTLAS's operations (rt_d3d12.hpp ptlas_ops_): the operation count at 0, the WRITE_INSTANCE operation at 16 (its
// argument count at 20), the arguments from 64 (104 bytes each: the CPU's, then these).
RWByteAddressBuffer operations : register(u6);
RWByteAddressBuffer place_state : register(u7);  // rt_place's counters: STATE_WRITTEN at 0 (read only here)
// This pass's own (rt_d3d12.hpp PlacedRoom::ptlas_diff): the slots active in the PTLAS's placed places after the last
// pass, its writes, the first instance its descs came from, then 16 bytes a group of 64 slots (its changed slots' two
// mask words, their count, the writes before the group's).
RWByteAddressBuffer diff : register(u8);

cbuffer Dispatch : register(b1) {
  uint mode;            // MODE_*, PTLAS_RESTART
  uint cpu_writes;      // the CPU's arguments (W_cpu, before these)
  uint first_instance;  // the CPU's instances (rt_place's descs follow them)
  uint place_end;       // rt_place's place_end (first_instance when it did not run this request)
  uint bound;           // the slots to look at: this request's and the last one's place_end - first_instance, the larger
  uint placed_base;     // the placed slots' first place (rt_d3d12.hpp PLACED_BASE)
  uint partition_size;  // rt_d3d12.hpp PTLAS_PARTITION_SIZE
  uint argument_room;   // the arguments the buffer holds (PLACED_BASE + the room's capacity)
};

static const uint MODE_MARK = 0u;
static const uint MODE_SCAN = 1u;
static const uint MODE_WRITE = 2u;
static const uint MODE_MASK = 15u;
static const uint PTLAS_RESTART = 16u;  // in `mode`: a fresh PTLAS (src 0), none of its placed places written yet
static const uint STATE_WRITTEN = 0u;  // rt_place.cs_6_5.hlsl
static const uint DIFF_ACTIVE = 0u;
static const uint DIFF_WRITES = 4u;
static const uint DIFF_FIRST = 8u;
static const uint DIFF_GROUPS = 16u;
static const uint OPERATION_COUNT = 0u;
static const uint OPERATION_ARGUMENT_COUNT = 20u;  // NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP::count at 16 + 4
static const uint ARGUMENTS = 64u;
// NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE (nvapi.h:21313-21324; rt_d3d12.hpp's static_asserts).
static const uint ARGUMENT_BYTES = 104u;
static const uint ARGUMENT_ID = 72u;
static const uint ARGUMENT_INDEX = 88u;  // instanceIndex, then partitionIndex and accelerationStructure (8 bytes)
static const uint FRAME_MASK = 0xFFFFFFu;
// NVAPI's instance flags are D3D12's bits (nvapi.h:21285-21288); rt_place sets TRIANGLE_CULL_DISABLE on every desc.
static const uint INSTANCE_CULL_DISABLE = 1u;
static const uint INSTANCE_FORCE_OPAQUE = 4u;
static const uint BLAS_BUILT_NOW = 64u;  // in a record's table word (rt_d3d12.hpp, M5a)

// An inactive argument, as the CPU writes one: identity rows, InstanceID, mask and flags 0, no BLAS ("When NULL the
// instance is inactive but still included in the build", nvapi.h:21324).
void StoreInactive(uint at, uint place, uint partition) {
  operations.Store4(at, asuint(float4(1.f, 0.f, 0.f, 0.f)));
  operations.Store4(at + 16u, asuint(float4(0.f, 1.f, 0.f, 0.f)));
  operations.Store4(at + 32u, asuint(float4(0.f, 0.f, 1.f, 0.f)));
  operations.Store4(at + 48u, uint4(0u, 0u, 0u, 0u));
  operations.Store2(at + 64u, uint2(0u, 0u));
  operations.Store4(at + ARGUMENT_ID, uint4(0u, 0u, 0u, 0u));
  operations.Store4(at + ARGUMENT_INDEX, uint4(place, partition, 0u, 0u));
}

groupshared uint shared_mask[2];
groupshared uint shared_values[64];
groupshared uint shared_total;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint slot = id.x + id.y * (65535u * 64u);  // rows of 65535 groups (D3D12's most along x; rt_place's index)
  const uint lane = id.x & 63u;
  const uint group = slot / 64u;
  const uint group_count = (bound + 63u) / 64u;
  // This request's placed descs: dense from first_instance, those past place_end not written (rt_place.cs_6_5.hlsl :468, :478).
  const uint active_now = (place_end > first_instance) ? min(place_state.Load(STATE_WRITTEN), place_end) - first_instance : 0u;
  if ((mode & MODE_MASK) == MODE_MARK) {
    // The rows end past the slots in whole groups (the group's syncs: none returns alone before them).
    if (group >= group_count) return;
    const uint active_before = ((mode & PTLAS_RESTART) != 0u) ? 0u : diff.Load(DIFF_ACTIVE);
    // Below the last count (every such slot is active): compared if this request has it too, else gone (an inactive write).
    bool changed = slot < active_before;
    if (slot < active_now) {
      const uint now_at = (first_instance + slot) * 64u;
      const uint4 now_tail = instances_written.Load4(now_at + 48u);
      if (changed) {
        // The PTLAS's fields against the slot's last write (rt_place's tail: InstanceID + mask, the frame + flags, the BLAS;
        // the frame is no field). Only below the last count: root descriptors have no bounds.
        const uint before_at = (diff.Load(DIFF_FIRST) + slot) * 64u;
        const uint4 before_tail = instances_before.Load4(before_at + 48u);
        changed = any(instances_written.Load4(now_at) != instances_before.Load4(before_at))
                  || any(instances_written.Load4(now_at + 16u) != instances_before.Load4(before_at + 16u))
                  || any(instances_written.Load4(now_at + 32u) != instances_before.Load4(before_at + 32u)) || now_tail.x != before_tail.x
                  || (now_tail.y >> 24) != (before_tail.y >> 24) || any(now_tail.zw != before_tail.zw);
      } else {
        changed = true;  // new
      }
      // A BLAS built this request at an address another one had: the instance keeps the bounds its last write saw, which
      // must cover its BLAS (nvapi.h:21289, :21323, :21334; X18's rule for the units, rt_d3d12.hpp).
      changed = changed || (table.Load((now_tail.x & FRAME_MASK) * 8u) & BLAS_BUILT_NOW) != 0u;
    }
    // rt_place's groupshared mask (rt_place.cs_6_5.hlsl :427-430).
    if (lane < 2u) shared_mask[lane] = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (changed) InterlockedOr(shared_mask[lane / 32u], 1u << (lane & 31u));
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
      diff.Store3(DIFF_GROUPS + group * 16u, uint3(shared_mask[0], shared_mask[1], countbits(shared_mask[0]) + countbits(shared_mask[1])));
    }
    return;
  }
  if ((mode & MODE_MASK) == MODE_SCAN) {
    // One group: each lane a run of groups (rt_place's last level, rt_place.cs_6_5.hlsl :457-466): its run's total, the
    // runs' places, then each group's.
    const uint run = (group_count + 63u) / 64u;
    const uint run_end = min((lane + 1u) * run, group_count);
    uint run_total = 0u;
    // A plain loop, as rt_place's: an unroll count caps a loop of unknown length at that many trips (DXC: 8 loads, no
    // back edge), so past 512 groups the count fell short and the build dropped the last arguments (G-B23 run 2, A7).
    for (uint at = lane * run; at < run_end; ++at) run_total += diff.Load(DIFF_GROUPS + at * 16u + 8u);
    shared_values[lane] = run_total;
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
      uint running = 0u;
      for (uint index = 0u; index < 64u; ++index) {
        const uint value = shared_values[index];
        shared_values[index] = running;
        running += value;
      }
      shared_total = running;
    }
    GroupMemoryBarrierWithGroupSync();
    uint running = shared_values[lane];
    for (uint at = lane * run; at < run_end; ++at) {
      diff.Store(DIFF_GROUPS + at * 16u + 12u, running);
      running += diff.Load(DIFF_GROUPS + at * 16u + 8u);
    }
    if (lane == 0u) {
      // Inert: a slot writes once and cpu_writes < placed_base, so the sum never passes the room; a count past it would
      // have the driver read past the buffer (a TDR).
      uint writes = min(cpu_writes + shared_total, argument_room);
      if (writes == 0u) {
        // No build without an operation: an update (src set) with operation count 0 is a case NVAPI does not describe,
        // so one inactive write to a spare place, as NVIDIA's vk_partitioned_tlas (Apache-2.0, src/partitioned_tlas.cpp
        // onRender) always sends one operation. placed_base - 1: inside every room (instanceCount = placed_base +
        // capacity), past the CPU's places (MAX_INSTANCES + CLUSTER_BLASES, rt_d3d12.hpp's static_assert) and below the
        // placed ones, so never written by anyone else: it stays inactive and the PTLAS's contents do not change.
        StoreInactive(ARGUMENTS, placed_base - 1u, (placed_base - 1u) / partition_size);
        writes = 1u;
      }
      operations.Store(OPERATION_ARGUMENT_COUNT, writes);
      operations.Store(OPERATION_COUNT, 1u);
      // The write mode reads only active_now: the next request's last count is safe to store now.
      diff.Store3(DIFF_ACTIVE, uint3(active_now, shared_total, first_instance));
    }
    return;
  }
  // MODE_WRITE: a changed slot's argument after the CPU's and the changed slots before it.
  if (group >= group_count) return;
  const uint4 group_changes = diff.Load4(DIFF_GROUPS + group * 16u);
  if (((((lane < 32u) ? group_changes.x : group_changes.y) >> (lane & 31u)) & 1u) == 0u) return;
  const uint before = (lane < 32u) ? countbits(group_changes.x & ((1u << lane) - 1u))
                                   : countbits(group_changes.x) + countbits(group_changes.y & ((1u << (lane - 32u)) - 1u));
  const uint written = cpu_writes + group_changes.w + before;
  if (written >= argument_room) return;  // inert (above)
  const uint place = placed_base + slot;
  // The partition: PTLAS_PARTITION_SIZE places in a row (ATN's Q2). B23b's world tiles replace this line alone.
  const uint partition = place / partition_size;
  const uint at = ARGUMENTS + written * ARGUMENT_BYTES;
  if (slot < active_now) {
    // transform: "Identical to Transform in D3D12_RAYTRACING_INSTANCE_DESC" (nvapi.h:21315); userAABB 0, ignored without
    // ENABLE_EXPLICIT_AABB (:21316); InstanceID and mask as the desc's (:21318-21319); hit group contribution 0, not the
    // desc's (:21320): there it is rt_place's frame drawn (rt_place.cs_6_5.hlsl:16-17), new for a drawn instance each
    // frame, which MARK leaves out (a write each frame otherwise) and no shader reads from the scene (the first-hit probe
    // alone prints it; compare_probe.py skips it at placed slots); flags: CULL_DISABLE and the desc's FORCE_OPAQUE (the
    // CPU's mapping); the BLAS (rt_place already cleared its flag bits).
    const uint now_at = (first_instance + slot) * 64u;
    const uint4 tail = instances_written.Load4(now_at + 48u);
    operations.Store4(at, instances_written.Load4(now_at));
    operations.Store4(at + 16u, instances_written.Load4(now_at + 16u));
    operations.Store4(at + 32u, instances_written.Load4(now_at + 32u));
    operations.Store4(at + 48u, uint4(0u, 0u, 0u, 0u));
    operations.Store2(at + 64u, uint2(0u, 0u));
    operations.Store4(at + ARGUMENT_ID, uint4(tail.x & FRAME_MASK, tail.x >> 24, 0u, INSTANCE_CULL_DISABLE | ((tail.y >> 24) & INSTANCE_FORCE_OPAQUE)));
    operations.Store4(at + ARGUMENT_INDEX, uint4(place, partition, tail.z, tail.w));
  } else {
    StoreInactive(at, place, partition);  // gone since the last request
  }
}
