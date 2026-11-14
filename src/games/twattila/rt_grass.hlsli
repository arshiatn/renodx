/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing's grass (rt_capture_grass_sm4 / sm5, rt_rows_grass): rigidgrass.hlsl's blades shrink with their instance's
// distance, flat at 100 m x Grass Height Distance (SI.grass_height_distance; 1: the game's). Here the whole blade's height
// about y = -0.6 (the game's shrinks toward its tip by COLOR0.w; the roots it keeps go under the terrain), to 5% at least:
// a placed copy scales its mesh (its first instance's shrink) by its own over that one, no divide by zero.
#ifndef SRC_GAMES_TWATTILA_RT_GRASS_HLSLI_
#define SRC_GAMES_TWATTILA_RT_GRASS_HLSLI_

#include "./shared.h"

float RtGrassShrink(float3 instance_position, float3 camera_position) {
  return max(0.05, 1 - min(1, distance(instance_position, camera_position) * (0.01 / SI.grass_height_distance)));
}

#endif  // SRC_GAMES_TWATTILA_RT_GRASS_HLSLI_
