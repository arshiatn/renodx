/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's placed meshes (dlaa_helper/rt_d3d12.hpp, bridge v14): the TLAS instance descs after the
// CPU's, in four passes (`mode`) over one list of items: this request's draws' instances (drawn), then, with
// PLACE_WITH_KEPT, the last dispatch's placed descs (kept: their mesh's BLAS as it is now, but those drawn more than
// `recent_frames` ago, of released meshes, of meshes drawn moving now, or undrawn inside the view: KEPT_IN_VIEW_FRAMES,
// at once where the frame drew none of their mesh or placed all of it). Clear (the dedupe table and its owners, the descs
// past the CPU's, the counters), claim (each item's key in the table, the lowest item its owner), count (the owners, by
// groups of 64, and their places: the groups' and blocks' prefix sums by the last group of each block and the last block)
// and write (each owner's desc at its place). Two runs of one request place the same descs in the same order, whatever
// order the GPU ran the threads in (the first-come dedupe and appends kept a different duplicate and order each time).
// A mesh's instance at one place (1 cm) once,
// the drawn first: instances stay while their mesh is drawn elsewhere (off-screen casters). Each desc keeps the frame
// it was drawn in (InstanceContributionToHitGroupIndex: no hit groups here). Degenerate rows: left out. A hidden draw's
// instances (swaying leaves, their compact copy traced instead) go in with mask 0 (no ray meets them; grass's: only the
// short ambient rays, GRASS_STILL_INSTANCE), kept ones shown.
// Grass outside the view or past Grass Ray Tracing Range (drawn or kept) stays out: GrassInScene. A hidden draw's instance
// SwayTraced turns down (its swaying copy left out: rt_sway.hlsli) is shown.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_place.h -Vn RT_PLACE.
#include "./rt_sway.hlsli"

ByteAddressBuffer instances_before : register(t0);  // the last dispatch's descs (the kept ones' source)
Buffer<float4> vertex_positions : register(t7);     // the request's vertices: an instance's rows 0-2 in four texels
// The records' BLAS addresses (two uints each: 0 not a placed mesh now, bit 0 drawn moving now, bit 1 grass, bit 2 drawn
// this frame, bit 3 all its instances placed this frame, bit 4 ever drawn moving, bit 5 a solid cut-out), then the jobs (two uint4 each: first drawn instance, instances, rows texel, flags; record, 0, 0, 0).
ByteAddressBuffer table : register(t8);
RWByteAddressBuffer instances_written : register(u6);  // D3D12_RAYTRACING_INSTANCE_DESC, 64 bytes each
RWStructuredBuffer<float4> build_rows : register(u7);  // three per record
// The counters (written, the last dispatch's written and CPU instances, this one's CPU instances), then the dedupe
// table (a key each, 0 empty).
RWByteAddressBuffer state : register(u8);
// The dedupe's scratch (rt_d3d12.hpp PlacedRoom::scratch, written anew each request: its parts below SCRATCH_OWNERS).
// Coherent: the count pass's last groups read the other groups' counts.
globallycoherent RWByteAddressBuffer scratch : register(u0, space1);

cbuffer Dispatch : register(b1) {
  uint mode;            // MODE_*, PLACE_WITH_KEPT
  uint job_count;
  uint instance_count;  // this request's drawn instances (the first items)
  uint first_instance;  // the CPU's instances (the placed descs follow them)
  uint frame;
  uint max_instances;
  uint recent_frames;
  uint record_count;  // the table's records (the jobs after them)
};

cbuffer Camera : register(b2) {
  float4 view_projection[4];  // the trace's camera registers: clip = (dot(position, [0]), ...); all 0: none
  uint sway_flags;            // rt_sway.hlsli
  float sway_range;
  uint table_entries;  // a power of two: rt_d3d12.hpp placed_capacity_ x 2
  float grass_range;   // Grass Ray Tracing Range (metres along the view; 0: any): rt_sway.hlsli InViewRange
};

static const uint MODE_CLEAR = 0u;
static const uint MODE_CLAIM = 1u;
static const uint MODE_COUNT = 2u;
static const uint MODE_WRITE = 3u;
static const uint MODE_MASK = 15u;
static const uint PLACE_WITH_KEPT = 16u;  // in `mode`: the kept items follow the drawn ones
static const uint PLACE_FOLIAGE = 1u;  // alpha-tested: not forced opaque
static const uint PLACE_HIDDEN = 2u;   // mask 0 while drawn
static const uint PLACE_GRASS = 4u;    // GRASS_INSTANCE (most rays pass it), kept GRASS_KEPT_FRAMES off-screen
static const uint PLACE_CUT_OUT = 8u;  // with PLACE_FOLIAGE: CUT_OUT_INSTANCE (a fence, a thatched wall: no light through it)
// In `sway_flags` beside rt_sway.hlsli's (dlaa_bridge.h RT_ONE_AT_A_PLACE): a kept instance goes where the frame placed another mesh.
static const uint PLACE_ONE_AT_A_PLACE = 4u;
static const uint INSTANCE_CULL_DISABLE = 1u;  // D3D12_RAYTRACING_INSTANCE_FLAG_*
static const uint INSTANCE_FORCE_OPAQUE = 4u;
// Instance masks (rt_d3d12.hpp): a solid mesh's, a leaves' mesh's (alpha-tested; Light Through Leaves counts them apart),
// grass's.
static const uint SOLID_INSTANCE = 0x01u;
static const uint LEAF_INSTANCE = 0x04u;
static const uint GRASS_INSTANCE = 0x08u;
static const uint CUT_OUT_INSTANCE = 0x40u;
static const uint GRASS_STILL_INSTANCE = 0x20u;
// Grass kept two turns (the addon places a still mesh every 4th frame): its shadows are short, and fields of it kept 5 s
// multiplied the instances every ray met (ATN's 2026-10-29 profile: 46-65 K placed).
static const uint GRASS_KEPT_FRAMES = 8u;
// A kept instance undrawn this many frames (two turns) with its origin inside the view is gone (the game drew another
// level of detail there, or nothing): kept recent_frames, a coarser copy darkened the finer one for seconds (ATN
// 2026-10-29: surfaces black a few seconds while zooming); at once where the frame drew none of its mesh or placed all of
// it (the addon places a still mesh every 4th frame, and the frame after its instances changed; until then not placed
// again need not mean gone). Off-screen casters stay.
static const uint KEPT_IN_VIEW_FRAMES = 8u;
// A mesh ever drawn moving, however long `recent_frames` keeps the rest: kept long, one walking off leaves a trail.
static const uint MOVED_KEPT_FRAMES = 300u;
static const uint STATE_WRITTEN = 0u;
static const uint STATE_BEFORE = 4u;
static const uint STATE_CPU_BEFORE = 8u;
static const uint STATE_CPU = 12u;
static const uint STATE_TABLE = 16u;
static const uint FRAME_MASK = 0xFFFFFFu;
static const float MAX_TRANSLATION = 2.0e5f;  // metres from the BLAS's place (battle maps: a few kilometres)
static const float MAX_SCALE = 1000.f;
// How far outside the view's sides a grass patch still goes in (metres): the patch's size and its shadow at a low sun.
static const float GRASS_VIEW_MARGIN = 16.f;
// The scratch's parts (bytes from 0: a table entry's owner, an item's slot, then the groups' and blocks' uint4s and the
// blocks counted; rt_d3d12.hpp MakePlacedRoom's room for them): the table's slots' owners (~item, the lowest item's:
// InterlockedMax; 0 none), each item's slot + 1 (0: not placed; ITEM_UNSLOTTED: placed past a full run of the table, as
// before without a dedupe), each group's of 64 items (its owners' bits, their count, its place in its block), each block's
// of 64 groups (its groups counted, its owners, its place), then the blocks counted.
static const uint SCRATCH_OWNERS = 0u;
static const uint ITEM_UNSLOTTED = 0xFFFFFFFFu;
static const uint TABLE_FULL = 0xFFFFFFFFu;

uint ScratchItems() {
  return table_entries * 4u;
}

uint ScratchGroups() {
  return table_entries * 8u;
}

uint ScratchBlocks() {
  return ScratchGroups() + table_entries / 4u;
}

// The items: the drawn instances, then (PLACE_WITH_KEPT) the last dispatch's descs from its CPU ones on; at most one a
// table entry (the scratch's room).
uint ItemCount() {
  return min(instance_count + (((mode & PLACE_WITH_KEPT) != 0u) ? max_instances : 0u), table_entries);
}

// False: a grass patch at `position` (its origin in the world) is outside the view's four sides moved out by
// GRASS_VIEW_MARGIN, or past `grass_range`. Its shadows are short, and Grass Always Drawn draws every cell, also behind
// the camera (ATN's debug28: trees' and grass's shadows flickering and gone with it).
bool GrassInScene(float3 position) {
  return InViewRange(position, view_projection[3], grass_range)
         && (!any(view_projection[3] != 0.f)
             || NearView(float4(position, 1.f), view_projection[0], view_projection[1], view_projection[3], GRASS_VIEW_MARGIN));
}

// `recent_frames` past the old rules' longest: kept until the scene ends (Host::keep_placed). A solid mesh's instance placed
// lately, not placed again, with one of its mesh placed now within a cell or two: moved (a rigid draw says nothing of it;
// off-screen, one more copy at each placement would stay for good). Leaves stay: a tree beside one the cascades dropped.
static const uint KEPT_LONG_FRAMES = 3600u;
static const float MOVED_CELL_METRES = 1.f;

// A desc's key: its record and its place in centimetres; grass's across only, in 10 cm cells (its blades' shrink with the
// camera's distance lifts it: a moving camera made a new key at each placement, its kept copy stayed beside the new one,
// twice the grass in the shadows and in every ray's TLAS until it stopped, ATN's debug33).
uint CellKey(uint record, int3 cell) {
  uint key = record * 0x9E3779B1u;
  key = (key ^ asuint(cell.x)) * 0x85EBCA77u;
  key = (key ^ asuint(cell.y)) * 0xC2B2AE3Du;
  key = (key ^ asuint(cell.z)) * 0x27D4EB2Fu;
  return (key ^ (key >> 15)) | 1u;
}

uint PlaceKey(uint record, float3 position, bool grass) {
  return CellKey(record, grass ? int3(floor(float3(position.x, 0.f, position.z) * 10.f)) : int3(floor(position * 100.f)));
}

// Past the records' 24 bits: never a PlaceKey's record.
uint MovedKey(uint record, int2 cell) {
  return CellKey(record | 0x80000000u, int3(cell.x, 0, cell.y));
}

// Where anything but grass was placed this frame, in 5 cm cells: never a PlaceKey's or a MovedKey's record.
uint ThingKey(int3 cell) {
  return CellKey(0xC0000000u, cell);
}

bool InTable(uint key) {
  uint slot = key & (table_entries - 1u);
  [loop]
  for (uint probe = 0u; probe < 32u; ++probe) {
    const uint found = state.Load(STATE_TABLE + slot * 4u);
    if (found == key) return true;
    if (found == 0u) return false;
    slot = (slot + 1u) & (table_entries - 1u);
  }
  return false;
}

// The key's slot in the table, put in where it was not (TABLE_FULL: a full run of the table).
uint Insert(uint key) {
  uint slot = key & (table_entries - 1u);
  [loop]
  for (uint probe = 0u; probe < 32u; ++probe) {
    uint previous = 0u;
    state.InterlockedCompareExchange(STATE_TABLE + slot * 4u, 0u, key, previous);
    if (previous == 0u || previous == key) return slot;
    slot = (slot + 1u) & (table_entries - 1u);
  }
  return TABLE_FULL;
}

// `item` placed at `key`: the lowest item placed at a key owns it (its slot + 1 for SCRATCH_ITEMS).
uint Claim(uint key, uint item) {
  const uint slot = Insert(key);
  if (slot == TABLE_FULL) return ITEM_UNSLOTTED;
  scratch.InterlockedMax(SCRATCH_OWNERS + slot * 4u, ~item);
  return slot + 1u;
}

// A drawn instance (`drawn_index` of this request's): its desc, its key, its job's flags and its place (false: left out).
bool DrawnInstance(uint drawn_index, out uint4 row0, out uint4 row1, out uint4 row2, out uint4 tail, out uint key, out uint job_flags,
                   out float3 position) {
  row0 = 0u;
  row1 = 0u;
  row2 = 0u;
  tail = 0u;
  key = 0u;
  job_flags = 0u;
  position = 0.f;
  const uint jobs = record_count * 8u;
  uint low = 0u;
  uint high = job_count - 1u;
  while (low < high) {
    const uint middle = (low + high + 1u) / 2u;
    if (table.Load(jobs + middle * 32u) <= drawn_index) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  const uint4 job = table.Load4(jobs + low * 32u);
  const uint record = table.Load(jobs + low * 32u + 16u);
  const uint instance = drawn_index - job.x;
  const uint2 blas = table.Load2(record * 8u);
  if (instance >= job.y || (blas.x | blas.y) == 0u) return false;
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
  job_flags = job.w;
  position = float3(now0.w, now1.w, now2.w);
  // Garbage rows (an instance moved far off or scaled past any mesh's size) leave it out: one spread the scene's bounds
  // until every ray met every instance.
  const float scale = max(max(length(rotation[0]), length(rotation[1])), length(rotation[2]));
  if (!(abs(determinant) > 9.997891e-13f) || !all(isfinite(rotation[0])) || !all(isfinite(rotation[1])) || !all(isfinite(rotation[2]))
      || !all(isfinite(translation)) || any(abs(translation) > MAX_TRANSLATION) || !(scale < MAX_SCALE)
      || ((job.w & PLACE_GRASS) != 0u && !GrassInScene(position))) {
    return false;
  }
  key = PlaceKey(record, translation, (job.w & PLACE_GRASS) != 0u);
  // InstanceID (the record) and mask (its group; 0 hidden); the frame drawn and the flags; the BLAS (its moving bit
  // cleared).
  const uint flags = INSTANCE_CULL_DISABLE | (((job.w & PLACE_FOLIAGE) != 0u) ? 0u : INSTANCE_FORCE_OPAQUE);
  const bool hidden = (job.w & PLACE_HIDDEN) != 0u
                      && ((sway_flags & SWAY_CUT) == 0u || SwayTraced(position, view_projection[0], view_projection[1], view_projection[3], sway_range, sway_flags));
  const uint mask = hidden                          ? (((job.w & PLACE_GRASS) != 0u) ? GRASS_STILL_INSTANCE : 0u)
                    : ((job.w & PLACE_GRASS) != 0u)  ? GRASS_INSTANCE
                    : ((job.w & PLACE_CUT_OUT) != 0u) ? CUT_OUT_INSTANCE
                    : ((job.w & PLACE_FOLIAGE) != 0u) ? LEAF_INSTANCE
                                                      : SOLID_INSTANCE;
  row0 = asuint(float4(rotation[0], translation.x));
  row1 = asuint(float4(rotation[1], translation.y));
  row2 = asuint(float4(rotation[2], translation.z));
  tail = uint4(record | (mask << 24), (frame & FRAME_MASK) | (flags << 24), blas.x & ~0xFFu, blas.y);
  return true;
}

// A kept instance (the last dispatch's desc `index`): its desc now, its key and what the count pass asks of it (grass, its
// mesh undrawn, its BLAS's origin in the world); false: not kept.
bool KeptInstance(uint index, out uint4 row0, out uint4 row1, out uint4 row2, out uint4 tail, out uint key, out bool grass, out bool undrawn,
                  out float4 place) {
  row0 = 0u;
  row1 = 0u;
  row2 = 0u;
  tail = 0u;
  key = 0u;
  grass = false;
  undrawn = false;
  place = 0.f;
  if (index < state.Load(STATE_CPU_BEFORE) || index >= state.Load(STATE_BEFORE) || index >= max_instances) return false;
  const uint4 before = instances_before.Load4(index * 64u + 48u);
  const uint record = before.x & FRAME_MASK;
  if ((before.z | before.w) == 0u || record >= record_count || ((frame - before.y) & FRAME_MASK) > recent_frames) return false;
  const uint2 blas = table.Load2(record * 8u);
  grass = (blas.x & 2u) != 0u;
  if ((blas.x | blas.y) == 0u || (blas.x & 1u) != 0u || (grass && ((frame - before.y) & FRAME_MASK) > GRASS_KEPT_FRAMES)
      || ((blas.x & 16u) != 0u && ((frame - before.y) & FRAME_MASK) > MOVED_KEPT_FRAMES)) {
    return false;
  }
  row0 = instances_before.Load4(index * 64u);
  row1 = instances_before.Load4(index * 64u + 16u);
  row2 = instances_before.Load4(index * 64u + 32u);
  // Its rows place the BLAS (its first instance in world space): its origin there, the build's, where they put it.
  const float4 origin = float4(build_rows[record * 3u].w, build_rows[record * 3u + 1u].w, build_rows[record * 3u + 2u].w, 1.f);
  place = float4(dot(asfloat(row0), origin), dot(asfloat(row1), origin), dot(asfloat(row2), origin), 1.f);
  // The frame drew none of its mesh, or placed all of it: not placed again means not drawn there.
  undrawn = (blas.x & 4u) == 0u || (blas.x & 8u) != 0u;
  const bool gone = (undrawn || ((frame - before.y) & FRAME_MASK) > KEPT_IN_VIEW_FRAMES) && any(view_projection[3] != 0.f)
                    && NearView(place, view_projection[0], view_projection[1], view_projection[3], 0.f);
  if ((grass && !GrassInScene(place.xyz)) || gone) return false;
  key = PlaceKey(record, asfloat(uint3(row0.w, row1.w, row2.w)), grass);
  const uint kept_mask = grass                                                  ? GRASS_INSTANCE
                         : ((blas.x & 32u) != 0u)                                ? CUT_OUT_INSTANCE
                         : ((((before.y >> 24) & INSTANCE_FORCE_OPAQUE) != 0u) ? SOLID_INSTANCE : LEAF_INSTANCE);
  tail = uint4(record | (kept_mask << 24), before.y, blas.x & ~0xFFu, blas.y);
  return true;
}

// A group's values in `shared_values` (one a thread) turned into their exclusive prefix sums; their total. Every thread of
// the group calls it.
groupshared uint shared_values[64];
groupshared uint shared_total;
uint ScanShared(uint lane) {
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
  return shared_total;
}

groupshared uint shared_mask[2];
groupshared bool shared_last;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const uint index = id.x + id.y * (65535u * 64u);  // rows of 65535 groups (D3D12's most along x)
  const uint item_count = ItemCount();
  const uint group_count = (item_count + 63u) / 64u;
  const uint block_count = (group_count + 63u) / 64u;
  const uint lane = id.x & 63u;
  const uint group = index / 64u;
  const uint block = group / 64u;
  if ((mode & MODE_MASK) == MODE_CLEAR) {
    if (index == 0u) {
      state.Store(STATE_BEFORE, min(state.Load(STATE_WRITTEN), max_instances));
      state.Store(STATE_CPU_BEFORE, state.Load(STATE_CPU));
      state.Store(STATE_WRITTEN, first_instance);
      state.Store(STATE_CPU, first_instance);
      scratch.Store(ScratchBlocks() + block_count * 16u, 0u);
    }
    if (index < table_entries / 4u) {
      state.Store4(STATE_TABLE + index * 16u, uint4(0u, 0u, 0u, 0u));
      scratch.Store4(SCRATCH_OWNERS + index * 16u, uint4(0u, 0u, 0u, 0u));
    }
    if (index < block_count) scratch.Store4(ScratchBlocks() + index * 16u, uint4(0u, 0u, 0u, 0u));
    if (index >= first_instance && index < max_instances) {
      [unroll]
      for (uint part = 0u; part < 64u; part += 16u) {
        instances_written.Store4(index * 64u + part, uint4(0u, 0u, 0u, 0u));
      }
    }
    return;
  }
  // The rows of 65535 groups end past the items in whole groups (the count pass's syncs: none returns alone before them).
  if (group >= group_count) return;
  const uint item = index;
  uint4 row0;
  uint4 row1;
  uint4 row2;
  uint4 tail;
  uint key;
  if ((mode & MODE_MASK) == MODE_CLAIM) {
    if (item >= item_count) return;
    uint slot_word = 0u;
    if (item < instance_count) {
      uint job_flags;
      float3 position;
      if (DrawnInstance(item, row0, row1, row2, tail, key, job_flags, position)) {
        slot_word = Claim(key, item);
        // Every drawn instance's moves and places (its duplicates' too: the same cells, whichever owns the key).
        if (recent_frames > KEPT_LONG_FRAMES && (job_flags & (PLACE_GRASS | PLACE_FOLIAGE)) == 0u) {
          Insert(MovedKey(tail.x & FRAME_MASK, int2(floor(position.xz / MOVED_CELL_METRES))));
        }
        if ((sway_flags & PLACE_ONE_AT_A_PLACE) != 0u && (job_flags & PLACE_GRASS) == 0u) Insert(ThingKey(int3(floor(position * 20.f))));
      }
    } else {
      bool grass;
      bool undrawn;
      float4 place;
      if (KeptInstance(state.Load(STATE_CPU_BEFORE) + (item - instance_count), row0, row1, row2, tail, key, grass, undrawn, place)) {
        slot_word = Claim(key, item);
      }
    }
    scratch.Store(ScratchItems() + item * 4u, slot_word);
    return;
  }
  if ((mode & MODE_MASK) == MODE_COUNT) {
    bool owner = false;
    if (item < item_count) {
      const uint slot_word = scratch.Load(ScratchItems() + item * 4u);
      owner = slot_word == ITEM_UNSLOTTED || (slot_word != 0u && scratch.Load(SCRATCH_OWNERS + (slot_word - 1u) * 4u) == ~item);
    }
    if (owner && item >= instance_count) {
      // A kept one goes where the frame placed another mesh (within 2.5 cm) while it wasn't: the game draws another level
      // of detail of the thing there, or what is left of it. Off-screen nothing else says so, and both stayed until the
      // scene ended. One placed lately and not again, beside one of its mesh placed now: moved.
      bool grass;
      bool undrawn;
      float4 place;
      KeptInstance(state.Load(STATE_CPU_BEFORE) + (item - instance_count), row0, row1, row2, tail, key, grass, undrawn, place);
      if ((sway_flags & PLACE_ONE_AT_A_PLACE) != 0u && !grass && undrawn) {
        const int3 cell = int3(floor(place.xyz * 20.f - 0.5f));
        [loop]
        for (uint corner = 0u; corner < 8u && owner; ++corner) {
          if (InTable(ThingKey(cell + int3(corner & 1u, (corner >> 1) & 1u, corner >> 2)))) owner = false;
        }
      }
      if (owner && recent_frames > KEPT_LONG_FRAMES && !grass && ((tail.y >> 24) & INSTANCE_FORCE_OPAQUE) != 0u
          && ((frame - tail.y) & FRAME_MASK) <= KEPT_IN_VIEW_FRAMES) {
        const int2 cell = int2(floor(place.xz / MOVED_CELL_METRES));
        [loop]
        for (int x = -1; x <= 1 && owner; ++x) {
          [loop]
          for (int z = -1; z <= 1 && owner; ++z) {
            if (InTable(MovedKey(tail.x & FRAME_MASK, cell + int2(x, z)))) owner = false;
          }
        }
      }
    }
    if (lane < 2u) shared_mask[lane] = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (owner) InterlockedOr(shared_mask[lane / 32u], 1u << (lane & 31u));
    GroupMemoryBarrierWithGroupSync();
    // Its owners counted, then (the last group of its block) the block's groups' places and its owners counted, then (the
    // last block) the blocks' places and the descs written.
    if (lane == 0u) {
      scratch.Store3(ScratchGroups() + group * 16u, uint3(shared_mask[0], shared_mask[1], countbits(shared_mask[0]) + countbits(shared_mask[1])));
      DeviceMemoryBarrier();
      uint counted;
      scratch.InterlockedAdd(ScratchBlocks() + block * 16u, 1u, counted);
      shared_last = counted + 1u == min(group_count - block * 64u, 64u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (!shared_last) return;
    DeviceMemoryBarrier();
    const uint in_block = block * 64u + lane;
    shared_values[lane] = (in_block < group_count) ? scratch.Load(ScratchGroups() + in_block * 16u + 8u) : 0u;
    const uint block_total = ScanShared(lane);
    if (in_block < group_count) scratch.Store(ScratchGroups() + in_block * 16u + 12u, shared_values[lane]);
    if (lane == 0u) scratch.Store(ScratchBlocks() + block * 16u + 4u, block_total);
    DeviceMemoryBarrierWithGroupSync();
    if (lane == 0u) {
      uint counted;
      scratch.InterlockedAdd(ScratchBlocks() + block_count * 16u, 1u, counted);
      shared_last = counted + 1u == block_count;
    }
    GroupMemoryBarrierWithGroupSync();
    if (!shared_last) return;
    DeviceMemoryBarrier();
    // Each thread a run of blocks: its run's total, the runs' places, then each block's.
    const uint run = (block_count + 63u) / 64u;
    uint run_total = 0u;
    for (uint at = lane * run; at < min((lane + 1u) * run, block_count); ++at) run_total += scratch.Load(ScratchBlocks() + at * 16u + 4u);
    shared_values[lane] = run_total;
    const uint total = ScanShared(lane);
    uint running = shared_values[lane];
    for (uint at = lane * run; at < min((lane + 1u) * run, block_count); ++at) {
      scratch.Store(ScratchBlocks() + at * 16u + 8u, running);
      running += scratch.Load(ScratchBlocks() + at * 16u + 4u);
    }
    if (lane == 0u) state.Store(STATE_WRITTEN, first_instance + total);
    return;
  }
  // MODE_WRITE: an owner's desc at the place of its group's and block's owners before it.
  if (item >= item_count) return;
  const uint4 group_owners = scratch.Load4(ScratchGroups() + group * 16u);
  if (((((lane < 32u) ? group_owners.x : group_owners.y) >> (lane & 31u)) & 1u) == 0u) return;
  const uint before = (lane < 32u) ? countbits(group_owners.x & ((1u << lane) - 1u))
                                   : countbits(group_owners.x) + countbits(group_owners.y & ((1u << (lane - 32u)) - 1u));
  const uint written = first_instance + scratch.Load(ScratchBlocks() + block * 16u + 8u) + group_owners.w + before;
  if (written >= max_instances) return;
  if (item < instance_count) {
    uint job_flags;
    float3 position;
    DrawnInstance(item, row0, row1, row2, tail, key, job_flags, position);
  } else {
    bool grass;
    bool undrawn;
    float4 place;
    KeptInstance(state.Load(STATE_CPU_BEFORE) + (item - instance_count), row0, row1, row2, tail, key, grass, undrawn, place);
  }
  instances_written.Store4(written * 64u, row0);
  instances_written.Store4(written * 64u + 16u, row1);
  instances_written.Store4(written * 64u + 32u, row2);
  instances_written.Store4(written * 64u + 48u, tail);
}
