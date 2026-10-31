/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila unit motion (object_motion.hpp): for a soldier or horse draw redrawn with its motion vertex shader, the motion
// and depth as motion_wind.ps, and its placement and bones recorded from every pixel (motion_unit_records.hlsli).
// Writing the table turns early depth off: hidden units are recorded too. With MOTION_UNIT_RECORDED
// (motion_unit_recorded.ps: Attila, whose draws stream their soldiers out: motion_unit_records.cs) this draws the motion
// alone, its hidden pixels skipped early.
#include "./motion.hlsli"
#ifndef MOTION_UNIT_RECORDED
#include "./motion_unit_records.hlsli"
#endif

uint2 main(float4 position : SV_Position, float4 clip : TEXCOORD0, float4 previous_clip : TEXCOORD1,
            nointerpolation float4 row0 : TEXCOORD2, nointerpolation float4 row1 : TEXCOORD3,
            nointerpolation float4 row2 : TEXCOORD4, nointerpolation float4 bones : TEXCOORD5) : SV_Target
{
#ifndef MOTION_UNIT_RECORDED
  RecordUnit(row0, row1, row2, bones, motion_unit.x, motion_unit.w, PlacementScale(motion_unit.z));
#endif
  return MotionTexel(MotionPixels(clip, previous_clip), position.z);
}
