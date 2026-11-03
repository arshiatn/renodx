/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila rigid motion (object_motion.hpp), once a frame after its draws: the rigid objects' records of this frame (their
// instances' world rows, by motion_record_*.vs through stream output, with the mesh key and the draw's slot) matched
// against last frame's table (motion.hlsli: the same mesh nearest where it was predicted to be; one not found yet is
// looked for further out, unless something else of the mesh is nearer), then put in this frame's table where they will
// be next frame (position + velocity), in every cell within PLACEMENT_REACH of it. Each draw's slot gets 1 when an
// instance moved, 2 when one stood still, 4 when one was not found (the CPU reads them back: which meshes move).
#include "./motion.hlsli"

Buffer<uint4> motion_records : register(t0);  // four per record: rows 0-2, (mesh key, draw slot, 0, 0)
RWBuffer<uint> motion_keys : register(u0);
RWBuffer<uint4> motion_placements : register(u1);
RWBuffer<uint> motion_slot_flags : register(u2);

cbuffer motion_rigid : register(b0)
{
  uint4 motion_rigid;  // x the records, y 1: last frame's table written, z 1: the campaign map (PlacementScale)
}

// Further out for a record not found by its prediction: a new fast object (its first frame had no velocity yet).
static const float BOOTSTRAP_REACH = 2.5f;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (id.x >= motion_rigid.x) return;
  const uint4 extra = motion_records[id.x * 4u + 3u];
  const uint mesh = extra.x;
  const uint slot = extra.y;
  float4 rows[3] = {asfloat(motion_records[id.x * 4u]), asfloat(motion_records[id.x * 4u + 1u]), asfloat(motion_records[id.x * 4u + 2u])};
  const float3 position = float3(rows[0].w, rows[1].w, rows[2].w);

  // Last frame's: by prediction within PLACEMENT_REACH, else (no velocity yet) further out.
  bool found = false;
  float3 previous_position = position;
  float4 previous_rows[3] = rows;
  const float scale = PlacementScale(motion_rigid.z);
  if (motion_rigid.y != 0u) {
    float nearest = PLACEMENT_REACH * PLACEMENT_REACH * scale * scale;
    const int2 center = int2(floor(position.xz / (PLACEMENT_CELL * scale)));
    const int reach = (int)ceil(BOOTSTRAP_REACH / PLACEMENT_CELL);
    for (int search = 0; search < 2 && !found; ++search) {
      const int spread = (search == 0) ? 0 : reach;
      if (search == 1) nearest = BOOTSTRAP_REACH * BOOTSTRAP_REACH * scale * scale;
      [loop]
      for (int z = -spread; z <= spread; ++z) {
        [loop]
        for (int x = -spread; x <= spread; ++x) {
          const uint first = PlacementCell(center + int2(x, z), mesh) * PLACEMENT_SLOTS;
          [loop]
          for (uint candidate = first; candidate < first + PLACEMENT_SLOTS; ++candidate) {
            if (motion_previous_keys[candidate] == 0u) break;
            const uint4 candidate_extra = motion_previous_placements[candidate * 4u + 3u];
            if (candidate_extra.x != mesh) continue;
            const float4 row0 = asfloat(motion_previous_placements[candidate * 4u]);
            const float4 row1 = asfloat(motion_previous_placements[candidate * 4u + 1u]);
            const float4 row2 = asfloat(motion_previous_placements[candidate * 4u + 2u]);
            const float3 velocity = asfloat(candidate_extra.yzw);
            // The second search only for objects without a velocity yet.
            if (search == 1 && any(velocity != 0.f)) continue;
            const float3 offset = float3(row0.w, row1.w, row2.w) + velocity - position;
            const float distance_squared = dot(offset, offset);
            if (distance_squared < nearest) {
              nearest = distance_squared;
              previous_rows[0] = row0;
              previous_rows[1] = row1;
              previous_rows[2] = row2;
              previous_position = float3(row0.w, row1.w, row2.w);
              found = true;
            }
          }
        }
      }
    }
  }
  const bool moved = found && (any(abs(rows[0] - previous_rows[0]) > 1e-4f) || any(abs(rows[1] - previous_rows[1]) > 1e-4f)
                               || any(abs(rows[2] - previous_rows[2]) > 1e-4f));
  uint ignored;
  InterlockedOr(motion_slot_flags[slot], found ? (moved ? 1u : 2u) : 4u, ignored);

  // This frame's record, where it will be next frame.
  const float3 velocity = found ? position - previous_position : 0.f;
  const float3 predicted = position + velocity;
  const uint key = ((mesh * 2654435761u) ^ (asuint(rows[0].w) * 2246822519u) ^ (asuint(rows[1].w) * 3266489917u)
                    ^ (asuint(rows[2].w) * 668265263u)) | 1u;
  MotionRecord(motion_keys, motion_placements, key, predicted.xz, rows, uint4(mesh, asuint(velocity)), scale);
}
