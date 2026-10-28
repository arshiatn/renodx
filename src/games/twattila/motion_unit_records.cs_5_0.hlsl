/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's records at the frame's end (object_motion.hpp Resolve): the soldiers its unit draws streamed out
// (motion_unit_point.vs: rows, then bone base, the animation texture's width, the draw's mesh and animation texture
// keys), a range of draws on one animation texture (t15) at a time, recorded as motion_unit_records.hlsli's.
#include "./motion_unit_records.hlsli"

Buffer<float4> unit_records : register(t0);

cbuffer unit_record_range : register(b0)
{
  uint4 unit_record_range;  // x the first record, y the records
}

[numthreads(64, 1, 1)]
void main(uint index : SV_DispatchThreadID)
{
  if (index >= unit_record_range.y) return;
  const uint record = (unit_record_range.x + index) * 4u;
  const float4 bones = unit_records[record + 3u];
  RecordUnit(unit_records[record], unit_records[record + 1u], unit_records[record + 2u], bones, asuint(bones.z), asuint(bones.w));
}
