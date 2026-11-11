/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's records (object_motion.hpp Resolve at the frame's end, FlushUnitRecords before the game packs its
// bones again): the soldiers its unit draws streamed out (motion_unit_point.vs: rows, then bone base, the animation
// texture's width, the draw's mesh and animation texture keys), a range of draws on one animation texture (t15) at a time,
// recorded as motion_unit_records.hlsli's.
#include "./motion_unit_records.hlsli"

Buffer<float4> unit_records : register(t0);

cbuffer unit_record_range : register(b0)
{
  uint4 unit_record_range;  // x the first record, y the records, z 1: the campaign map (PlacementScale), w 1: the game
                            // wrote their texture since their draw (other soldiers' bones now: placements alone)
}

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
  if (index >= unit_record_range.y) return;
  const uint record = (unit_record_range.x + index) * 4u;
  float4 bones = unit_records[record + 3u];
  // No width: ClaimBones claims nothing, so next frame the soldier takes this frame's pose (its key is this pack's alone).
  if (unit_record_range.w != 0u) bones.y = 0.f;
  RecordUnit(unit_records[record], unit_records[record + 1u], unit_records[record + 2u], bones, asuint(bones.z), asuint(bones.w),
             PlacementScale(unit_record_range.z));
}
