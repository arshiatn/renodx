/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's records (object_motion.hpp): a soldier's or horse's placement into this frame's table
// (motion.hlsli) in every cell within PLACEMENT_REACH of it, with its bones copied once per soldier (by bone key) into
// this frame's pool, for the next frame. motion_unit.ps records from every pixel of its redraw; Attila's draws stream
// their soldiers out instead (motion_unit_point.vs), recorded at the frame's end (motion_unit_records.cs).
#ifndef SRC_GAMES_TWATTILA_MOTION_UNIT_RECORDS_HLSLI_
#define SRC_GAMES_TWATTILA_MOTION_UNIT_RECORDS_HLSLI_

#include "./motion.hlsli"

Texture2D<float4> motion_animation_map : register(t15);  // the soldiers' draw's t_animation_map
RWBuffer<uint> motion_keys : register(u1);
RWBuffer<uint4> motion_placements : register(u2);
RWBuffer<float4> motion_bones : register(u3);
RWBuffer<uint> motion_bone_keys : register(u4);     // BONE_SLOTS keys, then the pool's count
RWBuffer<uint> motion_bone_entries : register(u5);  // BONE_SLOTS pool entries + 1 (0: none)

// The soldier's bones into the pool, by the caller that claims its key (another pixel or draw of it: already claimed).
void ClaimBones(uint key, float raw_base, float width)
{
  const uint base = (uint)max(floor(0.5f + raw_base), 0.f);
  const uint texture_width = (uint)max(floor(0.5f + width), 0.f);
  if (texture_width == 0u) return;
  [loop]
  for (uint probe = 0u; probe < BONE_PROBES; ++probe) {
    const uint slot = BoneSlot(key, probe);
    uint previous_key;
    InterlockedCompareExchange(motion_bone_keys[slot], 0u, key, previous_key);
    if (previous_key == key) return;
    if (previous_key != 0u) continue;
    uint entry;
    InterlockedAdd(motion_bone_keys[BONE_SLOTS], 1u, entry);
    if (entry >= BONE_SOLDIERS) return;
    // The texels as the game reads them: base + i along the rows of the texture, eight loads at a time.
    [loop]
    for (uint texel = 0u; texel < BONE_TEXELS; texel += 8u) {
      float4 values[8];
      [unroll]
      for (uint loaded = 0u; loaded < 8u; ++loaded) {
        const uint texel_index = base + texel + loaded;
        values[loaded] = motion_animation_map.Load(int3(texel_index % texture_width, texel_index / texture_width, 0));
      }
      [unroll]
      for (uint stored = 0u; stored < 8u; ++stored) {
        motion_bones[entry * BONE_TEXELS + texel + stored] = values[stored];
      }
    }
    motion_bone_entries[slot] = entry + 1u;
    return;
  }
}

// The unit's placement (its world rows) and bones (x its base in the animation texture, y the texture's width) of its
// draw's mesh and animation texture (object_motion.hpp's keys): one key per placement (every caller for the unit records
// the same; the first one in a slot wins it), its bones claimed by the caller that took a slot. `scale`: PlacementScale.
void RecordUnit(float4 row0, float4 row1, float4 row2, float4 bones, uint mesh_key, uint animation_key, float scale)
{
  const uint key = ((mesh_key * 2654435761u) ^ (asuint(row0.w) * 2246822519u) ^ (asuint(row1.w) * 3266489917u)
                    ^ (asuint(row2.w) * 668265263u)) | 1u;
  const uint bone_key = BoneKey(bones.x, animation_key);
  float4 rows[3] = {row0, row1, row2};
  if (MotionRecord(motion_keys, motion_placements, key, float2(row0.w, row2.w), rows,
                   uint4(mesh_key, bone_key, asuint(bones.x), asuint(bones.y)), scale)) {
    ClaimBones(bone_key, bones.x, bones.y);
  }
}

#endif  // SRC_GAMES_TWATTILA_MOTION_UNIT_RECORDS_HLSLI_
