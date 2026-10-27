/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion (object_motion.hpp): for a soldier or horse draw redrawn with its motion vertex shader, the motion
// and depth as motion_wind.ps, and its placement recorded in this frame's table (motion.hlsli) in every cell within
// PLACEMENT_REACH of it, with its bones copied once per soldier (by bone key) into this frame's pool, for the next
// frame. Writing the table turns early depth off: hidden units are recorded too.
#include "./motion.hlsli"

Texture2D<float4> motion_animation_map : register(t15);  // the draw's t_animation_map
RWBuffer<uint> motion_keys : register(u1);
RWBuffer<uint4> motion_placements : register(u2);
RWBuffer<float4> motion_bones : register(u3);
RWBuffer<uint> motion_bone_keys : register(u4);     // BONE_SLOTS keys, then the pool's count
RWBuffer<uint> motion_bone_entries : register(u5);  // BONE_SLOTS pool entries + 1 (0: none)

// The soldier's bones into the pool, by the pixel that claims its key (another pixel or draw of it: already claimed).
void ClaimBones(uint key, float raw_base, float width)
{
  const int base = (int)floor(0.5f + raw_base);
  const int texture_width = (int)floor(0.5f + width);
  if (texture_width <= 0) return;
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
    // The texels as the game reads them: base + i along the rows of the texture.
    [loop]
    for (uint texel = 0u; texel < BONE_TEXELS; ++texel) {
      const int texel_index = base + (int)texel;
      motion_bones[entry * BONE_TEXELS + texel] = motion_animation_map.Load(int3(texel_index % texture_width, texel_index / texture_width, 0));
    }
    motion_bone_entries[slot] = entry + 1u;
    return;
  }
}

uint2 main(float4 position : SV_Position, float4 clip : TEXCOORD0, float4 previous_clip : TEXCOORD1,
            nointerpolation float4 row0 : TEXCOORD2, nointerpolation float4 row1 : TEXCOORD3,
            nointerpolation float4 row2 : TEXCOORD4, nointerpolation float4 bones : TEXCOORD5) : SV_Target
{
  // One key per placement (every pixel of the unit writes the same record; the first one in a slot wins it), its bones
  // claimed by the pixel that took a slot.
  const uint key = ((motion_unit.x * 2654435761u) ^ (asuint(row0.w) * 2246822519u) ^ (asuint(row1.w) * 3266489917u)
                    ^ (asuint(row2.w) * 668265263u)) | 1u;
  const uint bone_key = BoneKey(bones.x);
  float4 rows[3] = {row0, row1, row2};
  if (MotionRecord(motion_keys, motion_placements, key, float2(row0.w, row2.w), rows,
                   uint4(motion_unit.x, bone_key, asuint(bones.x), asuint(bones.y)))) {
    ClaimBones(bone_key, bones.x, bones.y);
  }
  return MotionTexel(MotionPixels(clip, previous_clip), position.z);
}
