/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion's records (object_motion.hpp): one point per soldier or horse (motion_unit_point.vs, a pixel of
// its own, no depth, no target) records its placement and bones (motion_unit_records.hlsli).
#include "./motion_unit_records.hlsli"

void main(float4 position : SV_Position, nointerpolation float4 row0 : TEXCOORD2, nointerpolation float4 row1 : TEXCOORD3,
          nointerpolation float4 row2 : TEXCOORD4, nointerpolation float4 bones : TEXCOORD5)
{
  RecordUnit(row0, row1, row2, bones);
}
