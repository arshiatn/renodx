/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's camera constants (b3): the paths' (rt_path.hlsli) and the trace's ambient history
// (rt_trace.cs_6_5.hlsl), with the projection of a world position by a camera's registers.
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_

cbuffer Paths : register(b3) {
  float4 view_projection[4];           // this frame's camera registers: clip = (dot(position, [0]), ...)
  float4 previous_view_projection[4];  // last frame's
  uint bounce_width;
  uint bounce_height;
  uint bounce_scale;
  uint path_flags;  // PATH_* (rt_path.hlsli, and below)
  uint path_bounces;
  uint path_samples;
  float bounce_range;      // metres
  float reflection_range;  // metres (specular occlusion's reach)
};
static const uint PATH_AMBIENT_HISTORY = 16384u;  // path_flags in the trace: last trace's ambient and positions are valid
static const uint PATH_AMBIENT_PARITY = 32768u;   // its history read from set 1 (else 0)

float4 ClipOf(float3 world, float4 registers[4]) {
  const float4 position = float4(world, 1.f);
  return float4(dot(position, registers[0]), dot(position, registers[1]), dot(position, registers[2]), dot(position, registers[3]));
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_
