/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's placed meshes (dlaa_helper/rt_d3d12.hpp, bridge v14): the TLAS instance descs after the
// CPU's, in three passes (`mode`): clear (the dedupe table, the descs past the CPU's, the counters), drawn (this
// request's draws' instances: their rows times the inverse of their mesh's build rows, its BLAS its first instance in
// world space) and kept (the last dispatch's placed descs: their mesh's BLAS as it is now, but those drawn more than
// `recent_frames` ago, of released meshes or of meshes drawn moving now). A mesh's instance at one place (1 cm) once,
// the drawn first: instances stay while their mesh is drawn elsewhere (off-screen casters). Each desc keeps the frame
// it was drawn in (InstanceContributionToHitGroupIndex: no hit groups here). Degenerate rows: left out. A hidden draw's
// instances (swaying leaves, their compact copy traced instead) go in with mask 0 (no ray meets them), kept ones shown.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_place.h -Vn RT_PLACE.

ByteAddressBuffer instances_before : register(t0);  // the last dispatch's descs (the kept ones' source)
Buffer<float4> vertex_positions : register(t7);     // the request's vertices: an instance's rows 0-2 in four texels
// The records' BLAS addresses (two uints each: 0 not a placed mesh now, bit 0 drawn moving now), then the jobs (two
// uint4 each: first drawn instance, instances, rows texel, flags; record, 0, 0, 0).
ByteAddressBuffer table : register(t8);
RWByteAddressBuffer instances_written : register(u6);  // D3D12_RAYTRACING_INSTANCE_DESC, 64 bytes each
RWStructuredBuffer<float4> build_rows : register(u7);  // three per record
// The counters (written, the last dispatch's written and CPU instances, this one's CPU instances), then the dedupe
// table (a key each, 0 empty).
RWByteAddressBuffer state : register(u8);

cbuffer Dispatch : register(b1) {
  uint mode;
  uint job_count;
  uint instance_count;  // drawn: this request's drawn instances
  uint first_instance;  // the CPU's instances
  uint frame;
  uint max_instances;
  uint recent_frames;
  uint record_count;  // the table's records (the jobs after them)
};

static const uint MODE_CLEAR = 0u;
static const uint MODE_DRAWN = 1u;
static const uint MODE_KEPT = 2u;
static const uint PLACE_FOLIAGE = 1u;  // alpha-tested: not forced opaque
static const uint PLACE_HIDDEN = 2u;   // mask 0 while drawn
static const uint INSTANCE_CULL_DISABLE = 1u;  // D3D12_RAYTRACING_INSTANCE_FLAG_*
static const uint INSTANCE_FORCE_OPAQUE = 4u;
// Instance masks (rt_d3d12.hpp): a solid mesh's, a leaves' mesh's (alpha-tested; Light Through Leaves counts them apart).
static const uint SOLID_INSTANCE = 0x01u;
static const uint LEAF_INSTANCE = 0x04u;
static const uint STATE_WRITTEN = 0u;
static const uint STATE_BEFORE = 4u;
static const uint STATE_CPU_BEFORE = 8u;
static const uint STATE_CPU = 12u;
static const uint STATE_TABLE = 16u;
static const uint TABLE_ENTRIES = 1u << 18;  // rt_d3d12.hpp PLACE_TABLE
static const uint FRAME_MASK = 0xFFFFFFu;
static const float MAX_TRANSLATION = 2.0e5f;  // metres from the BLAS's place (battle maps: a few kilometres)
static const float MAX_SCALE = 1000.f;

// A desc's key: its record and its place in centimetres.
uint PlaceKey(uint record, float3 position) {
  const int3 cell = int3(floor(position * 100.f));
  uint key = record * 0x9E3779B1u;
  key = (key ^ asuint(cell.x)) * 0x85EBCA77u;
  key = (key ^ asuint(cell.y)) * 0xC2B2AE3Du;
  key = (key ^ asuint(cell.z)) * 0x27D4EB2Fu;
  return (key ^ (key >> 15)) | 1u;
}

// False: a desc with this key is in already (a full run of the table takes it in).
bool Insert(uint key) {
  uint slot = key & (TABLE_ENTRIES - 1u);
  [loop]
  for (uint probe = 0u; probe < 32u; ++probe) {
    uint previous = 0u;
    state.InterlockedCompareExchange(STATE_TABLE + slot * 4u, 0u, key, previous);
    if (previous == 0u) return true;
    if (previous == key) return false;
    slot = (slot + 1u) & (TABLE_ENTRIES - 1u);
  }
  return true;
}

void Append(uint4 row0, uint4 row1, uint4 row2, uint4 tail) {
  uint written = 0u;
  state.InterlockedAdd(STATE_WRITTEN, 1u, written);
  if (written >= max_instances) return;
  instances_written.Store4(written * 64u, row0);
  instances_written.Store4(written * 64u + 16u, row1);
  instances_written.Store4(written * 64u + 32u, row2);
  instances_written.Store4(written * 64u + 48u, tail);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint index = id.x;
  if (mode == MODE_CLEAR) {
    if (index == 0u) {
      state.Store(STATE_BEFORE, min(state.Load(STATE_WRITTEN), max_instances));
      state.Store(STATE_CPU_BEFORE, state.Load(STATE_CPU));
      state.Store(STATE_WRITTEN, first_instance);
      state.Store(STATE_CPU, first_instance);
    }
    if (index < TABLE_ENTRIES / 4u) state.Store4(STATE_TABLE + index * 16u, uint4(0u, 0u, 0u, 0u));
    if (index >= first_instance && index < max_instances) {
      [unroll]
      for (uint part = 0u; part < 64u; part += 16u) {
        instances_written.Store4(index * 64u + part, uint4(0u, 0u, 0u, 0u));
      }
    }
    return;
  }
  if (mode == MODE_KEPT) {
    if (index < state.Load(STATE_CPU_BEFORE) || index >= state.Load(STATE_BEFORE)) return;
    const uint4 tail = instances_before.Load4(index * 64u + 48u);
    const uint record = tail.x & FRAME_MASK;
    if ((tail.z | tail.w) == 0u || record >= record_count || ((frame - tail.y) & FRAME_MASK) > recent_frames) return;
    const uint2 blas = table.Load2(record * 8u);
    if ((blas.x | blas.y) == 0u || (blas.x & 1u) != 0u) return;
    const uint4 row0 = instances_before.Load4(index * 64u);
    const uint4 row1 = instances_before.Load4(index * 64u + 16u);
    const uint4 row2 = instances_before.Load4(index * 64u + 32u);
    if (!Insert(PlaceKey(record, asfloat(uint3(row0.w, row1.w, row2.w))))) return;
    const uint kept_mask = (((tail.y >> 24) & INSTANCE_FORCE_OPAQUE) != 0u) ? SOLID_INSTANCE : LEAF_INSTANCE;
    Append(row0, row1, row2, uint4(record | (kept_mask << 24), tail.y, blas.x & ~0xFFu, blas.y));
    return;
  }
  if (index >= instance_count) return;
  const uint jobs = record_count * 8u;
  uint low = 0u;
  uint high = job_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (table.Load(jobs + middle * 32u) <= index) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 job = table.Load4(jobs + low * 32u);
  const uint record = table.Load(jobs + low * 32u + 16u);
  const uint instance = index - job.x;
  const uint2 blas = table.Load2(record * 8u);
  if (instance >= job.y || (blas.x | blas.y) == 0u) return;
  const float4 build0 = build_rows[record * 3u];
  const float4 build1 = build_rows[record * 3u + 1u];
  const float4 build2 = build_rows[record * 3u + 2u];
  const float4 now0 = vertex_positions[job.z + instance * 4u];
  const float4 now1 = vertex_positions[job.z + instance * 4u + 1u];
  const float4 now2 = vertex_positions[job.z + instance * 4u + 2u];
  // The build's 3 x 3 inverted (its cofactors' columns over its determinant), then its translation.
  const float3 cofactor0 = cross(build1.xyz, build2.xyz);
  const float3 cofactor1 = cross(build2.xyz, build0.xyz);
  const float3 cofactor2 = cross(build0.xyz, build1.xyz);
  const float determinant = dot(build0.xyz, cofactor0);
  const float3x3 inverse = transpose(float3x3(cofactor0, cofactor1, cofactor2)) / determinant;
  const float3x3 rotation = mul(float3x3(now0.xyz, now1.xyz, now2.xyz), inverse);
  const float3 translation = float3(now0.w, now1.w, now2.w) - mul(rotation, float3(build0.w, build1.w, build2.w));
  // Garbage rows (an instance moved far off or scaled past any mesh's size) leave it out: one spread the scene's bounds
  // until every ray met every instance.
  const float scale = max(max(length(rotation[0]), length(rotation[1])), length(rotation[2]));
  if (!(abs(determinant) > 1e-12f) || !all(isfinite(rotation[0])) || !all(isfinite(rotation[1])) || !all(isfinite(rotation[2]))
      || !all(isfinite(translation)) || any(abs(translation) > MAX_TRANSLATION) || !(scale < MAX_SCALE)
      || !Insert(PlaceKey(record, translation))) {
    return;
  }
  // InstanceID (the record) and mask (its group; 0 hidden); the frame drawn and the flags; the BLAS (its moving bit
  // cleared).
  const uint flags = INSTANCE_CULL_DISABLE | (((job.w & PLACE_FOLIAGE) != 0u) ? 0u : INSTANCE_FORCE_OPAQUE);
  const uint mask = ((job.w & PLACE_HIDDEN) != 0u) ? 0u : (((job.w & PLACE_FOLIAGE) != 0u) ? LEAF_INSTANCE : SOLID_INSTANCE);
  Append(asuint(float4(rotation[0], translation.x)), asuint(float4(rotation[1], translation.y)), asuint(float4(rotation[2], translation.z)),
         uint4(record | (mask << 24), (frame & FRAME_MASK) | (flags << 24), blas.x & ~0xFFu, blas.y));
}
