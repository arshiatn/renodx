/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila wind motion (object_motion.hpp): for a swaying grass, leaf or branch draw redrawn with its motion vertex shader
// (motion_*.vs), the motion in render pixels to where the surface was last frame (x right, y down, without the jitter),
// and its depth (MotionTexel; DLSS's preparation keeps the motion only where the final depth is this one: leaf cut-outs and
// anything drawn in front later fall back to the camera's motion).
#include "./motion.hlsli"

uint2 main(float4 position : SV_Position, float4 clip : TEXCOORD0, float4 previous_clip : TEXCOORD1) : SV_Target
{
  return MotionTexel(MotionPixels(clip, previous_clip), position.z);
}
