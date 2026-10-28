/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Object motion (object_motion.hpp): the constants of the motion shaders (motion_*.vs, motion_wind.ps,
// motion_unit.ps, motion_capture.ps, motion_rigid.cs) and the placement table. A unit's placement is its instance's world rows (TEXCOORD0-2 of the
// game's skinned shaders; y up, metres) and its bone base (TEXCOORD3.x: its bones start there in the animation texture,
// three texels each); each frame the units' redraw records every placement in the table's cells within PLACEMENT_REACH
// of it, and its bones once per soldier in a pool found by bone key (motion_unit.ps); the next frame each unit takes last
// frame's placement of the same mesh nearest its own (MotionPreviousPlacement) and skins its vertices with last frame's
// bones there: nothing else names a soldier from one frame to the next.
#ifndef SRC_GAMES_TWATTILA_MOTION_HLSLI_
#define SRC_GAMES_TWATTILA_MOTION_HLSLI_

cbuffer motion_VS : register(b12)
{
  float4 motion_current[4];   // the camera's view-projection rows (Attila's c9-c12 without the jitter)
  float4 motion_previous[4];  // last frame's
  float4 motion_times;        // x last frame's time_in_sec, zw the render size
  uint4 motion_unit;          // x the draw's mesh key, y 1: last frame's placements recorded, w units: the draw's
                              // animation texture (part of the bone key), rigid objects and deformed meshes: the draw's slot
  uint4 motion_draw;          // deformed meshes: x the draw's first position, y its positions, z its first vertex
  float4 motion_jitter;       // the rows' jitter in clip space (xy this frame's, zw last frame's; Shogun 2), else 0
  float4 motion_game_jitter;  // in-draw motion (motion_draw_*.vs): the jitter the game's rows hold (xy, clip per w)
}

// The motion in render pixels from a surface's clip position now to where it was last frame (x right, y down, without
// the jitter).
float2 MotionPixels(float4 clip, float4 previous_clip)
{
  const float2 now = clip.xy / clip.w - motion_jitter.xy;
  const float2 before = previous_clip.xy / previous_clip.w - motion_jitter.zw;
  const float2 motion = (before - now) * float2(0.5f, -0.5f) * motion_times.zw;
  return all(isfinite(motion)) ? motion : 0.f;
}

// The motion target's texel (object_motion.hpp, R32G32_UINT: half the bytes of four floats): the motion as two halves,
// the depth's bits (0: not written).
uint2 MotionTexel(float2 motion, float depth)
{
  return uint2(f32tof16(motion.x) | (f32tof16(motion.y) << 16), asuint(depth));
}

bool MotionTexelWritten(uint2 texel)
{
  return texel.y != 0u;
}

float2 MotionTexelMotion(uint2 texel)
{
  return float2(f16tof32(texel.x), f16tof32(texel.x >> 16));
}

float MotionTexelDepth(uint2 texel)
{
  return asfloat(texel.y);
}

// Whether a motion target texel is of the final surface: its depth is the final depth (the redraw's can differ from the
// game's in the last bits; standard 24-bit depth).
bool MotionDepthMatches(float motion_depth, float depth)
{
  return abs(motion_depth - depth) <= 4e-6f;
}

// The table's cells (a power of two) and placements per cell; a unit moves less than PLACEMENT_REACH in a frame, and
// soldiers stand further apart.
static const uint PLACEMENT_CELLS = 65536u;
static const uint PLACEMENT_SLOTS = 8u;
static const float PLACEMENT_REACH = 0.5f;
static const float PLACEMENT_CELL = 1.f;
// The bone pool: a soldier's first BONE_TEXELS texels (96 bones) from its bone base, BONE_SOLDIERS soldiers a frame,
// found by bone key in a table of BONE_SLOTS.
static const uint BONE_TEXELS = 288u;
static const uint BONE_SOLDIERS = 16384u;
static const uint BONE_SLOTS = 32768u;
static const uint BONE_PROBES = 16u;

uint PlacementCell(int2 cell)
{
  return ((asuint(cell.x) * 73856093u) ^ (asuint(cell.y) * 19349663u)) & (PLACEMENT_CELLS - 1u);
}

// A placement into this frame's table (`keys`: a key per slot, 0 empty; `placements`: four rows per slot) in every cell
// within PLACEMENT_REACH of `position` (x, z), under `key` (one per placement: its first writer in a cell takes a slot,
// later ones find it); true when this call took one.
bool MotionRecord(RWBuffer<uint> keys, RWBuffer<uint4> placements, uint key, float2 position, float4 rows[3], uint4 extra)
{
  bool took = false;
  const float2 low = floor((position - PLACEMENT_REACH) / PLACEMENT_CELL);
  const float2 high = floor((position + PLACEMENT_REACH) / PLACEMENT_CELL);
  [loop]
  for (float z = low.y; z <= high.y; ++z) {
    [loop]
    for (float x = low.x; x <= high.x; ++x) {
      const uint first = PlacementCell(int2(x, z)) * PLACEMENT_SLOTS;
      [loop]
      for (uint slot = first; slot < first + PLACEMENT_SLOTS; ++slot) {
        // A plain read first: every pixel of a unit records the same key, and atomics on the same slots from all of them
        // serialized (a screen full of soldiers cost ~30 ms at 4K). Only an empty slot takes the atomic.
        const uint seen = keys[slot];
        if (seen == key) break;
        if (seen != 0u) continue;
        uint previous_key;
        InterlockedCompareExchange(keys[slot], 0u, key, previous_key);
        if (previous_key == key) break;
        if (previous_key != 0u) continue;
        placements[slot * 4u] = asuint(rows[0]);
        placements[slot * 4u + 1u] = asuint(rows[1]);
        placements[slot * 4u + 2u] = asuint(rows[2]);
        placements[slot * 4u + 3u] = extra;
        took = true;
        break;
      }
    }
  }
  return took;
}

// A soldier's bones this frame: its bone base in the draw's animation texture.
uint BoneKey(float raw_base)
{
  return ((asuint((int)floor(0.5f + raw_base)) * 2654435761u) ^ (motion_unit.w * 40503u)) | 1u;
}

uint BoneSlot(uint key, uint probe)
{
  return (key + probe * 7919u) % BONE_SLOTS;
}

// Last frame's table: per slot a key (0: empty) and four rows as uints: the world rows, then (the mesh key, the bone key,
// the bone base as the instance had it, the animation texture's width); its bone keys (BONE_SLOTS, then the pool's
// count) and their pool entries + 1 (0: none), and the pool (BONE_TEXELS per entry).
Buffer<uint> motion_previous_bone_keys : register(t11);
Buffer<uint> motion_previous_bone_entries : register(t12);
Buffer<float4> motion_previous_bones : register(t13);
Buffer<uint> motion_previous_keys : register(t14);
Buffer<uint4> motion_previous_placements : register(t15);

// Last frame's placement of this draw's mesh nearest `rows`' (within PLACEMENT_REACH), and its bone record; false: none.
bool MotionPreviousPlacement(float4 rows[3], out float4 previous[3], out uint4 previous_bones)
{
  previous = rows;
  previous_bones = 0u;
  if (motion_unit.y == 0u) return false;
  const float2 position = float2(rows[0].w, rows[2].w);
  const uint first = PlacementCell(int2(floor(position / PLACEMENT_CELL))) * PLACEMENT_SLOTS;
  float nearest = PLACEMENT_REACH * PLACEMENT_REACH;
  bool found = false;
  [loop]
  for (uint slot = first; slot < first + PLACEMENT_SLOTS; ++slot) {
    if (motion_previous_keys[slot] == 0u) break;
    const uint4 extra = motion_previous_placements[slot * 4u + 3u];
    if (extra.x != motion_unit.x) continue;
    const float4 row0 = asfloat(motion_previous_placements[slot * 4u]);
    const float4 row1 = asfloat(motion_previous_placements[slot * 4u + 1u]);
    const float4 row2 = asfloat(motion_previous_placements[slot * 4u + 2u]);
    const float3 offset = float3(row0.w, row1.w, row2.w) - float3(rows[0].w, rows[1].w, rows[2].w);
    const float distance_squared = dot(offset, offset);
    if (distance_squared < nearest) {
      nearest = distance_squared;
      previous[0] = row0;
      previous[1] = row1;
      previous[2] = row2;
      previous_bones = extra;
      found = true;
    }
  }
  return found;
}

// A rigid object (rigidcommon's instance rows, POSITION1-3) moves on its own (motion_rigid.cs): its records are
// (rows, (mesh key, velocity per frame)) and stand where it will be next frame (position + velocity), so fast ones
// (projectiles) are found again too. Last frame's placement of this draw's mesh nearest `rows`' by that prediction.
bool MotionPreviousRigidPlacement(float4 rows[3], out float4 previous[3])
{
  previous = rows;
  if (motion_unit.y == 0u) return false;
  const float3 position = float3(rows[0].w, rows[1].w, rows[2].w);
  const uint first = PlacementCell(int2(floor(position.xz / PLACEMENT_CELL))) * PLACEMENT_SLOTS;
  float nearest = PLACEMENT_REACH * PLACEMENT_REACH;
  bool found = false;
  [loop]
  for (uint slot = first; slot < first + PLACEMENT_SLOTS; ++slot) {
    if (motion_previous_keys[slot] == 0u) break;
    const uint4 extra = motion_previous_placements[slot * 4u + 3u];
    if (extra.x != motion_unit.x) continue;
    const float4 row0 = asfloat(motion_previous_placements[slot * 4u]);
    const float4 row1 = asfloat(motion_previous_placements[slot * 4u + 1u]);
    const float4 row2 = asfloat(motion_previous_placements[slot * 4u + 2u]);
    const float3 offset = float3(row0.w, row1.w, row2.w) + asfloat(extra.yzw) - position;
    const float distance_squared = dot(offset, offset);
    if (distance_squared < nearest) {
      nearest = distance_squared;
      previous[0] = row0;
      previous[1] = row1;
      previous[2] = row2;
      found = true;
    }
  }
  return found;
}

// A deformed mesh (cloth, sails, ropes: vertices the CPU moves each frame) is captured each frame (motion_capture_*.vs,
// motion_capture.ps): its world positions by vertex from its first position, and its anchor (its first index's vertex)
// by draw slot and in the table, as (mesh key, first position, positions, anchor vertex) at the anchor. Last frame's
// positions, this frame's anchors.
Buffer<float4> motion_previous_positions : register(t9);
Buffer<float4> motion_anchors : register(t10);

// Last frame's world position of this draw's `vertex` (the same mesh's anchor nearest this frame's); false: none.
bool MotionPreviousDeformed(uint vertex, out float4 previous_world)
{
  previous_world = 0.f;
  const float4 anchor = motion_anchors[motion_unit.w];
  float4 rows[3] = {float4(1.f, 0.f, 0.f, anchor.x), float4(0.f, 1.f, 0.f, anchor.y), float4(0.f, 0.f, 1.f, anchor.z)};
  float4 previous_rows[3];
  uint4 previous;
  if (!MotionPreviousPlacement(rows, previous_rows, previous) || vertex >= previous.z) return false;
  previous_world = motion_previous_positions[previous.y + vertex];
  return true;
}

// Last frame's pool entry + 1 for a bone key (0: none, or the pool was full).
uint MotionPreviousBoneEntry(uint key)
{
  [loop]
  for (uint probe = 0u; probe < BONE_PROBES; ++probe) {
    const uint slot = BoneSlot(key, probe);
    const uint found = motion_previous_bone_keys[slot];
    if (found == key) return motion_previous_bone_entries[slot];
    if (found == 0u) return 0u;
  }
  return 0u;
}

#endif  // SRC_GAMES_TWATTILA_MOTION_HLSLI_
