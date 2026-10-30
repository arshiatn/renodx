/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's camera constants (b3): the paths' (rt_path.hlsli) and the trace's histories
// (rt_trace.cs_6_5.hlsl, rt_lights.cs_6_5.hlsl), with the projection of a world position by a camera's registers and
// where the last trace saw it (after rt_scene.hlsli: the size).
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
static const uint PATH_LIGHT_HISTORY = 131072u;   // the last trace's positions and local lights' bits are valid
static const uint PATH_TINT = 1048576u;           // Coloured Shadows: the trace writes its filter (rt_trace's tint)
static const uint PATH_TINT_GUIDES = 2097152u;    // with the diffuse albedo guide's colours

RWTexture2D<float4> ambient_positions : register(u30);  // the last trace's positions (its histories')

float4 ClipOf(float3 world, float4 registers[4]) {
  const float4 position = float4(world, 1.f);
  return float4(dot(position, registers[0]), dot(position, registers[1]), dot(position, registers[2]), dot(position, registers[3]));
}

// Where the last trace saw `world` (its pixel), if it saw the same surface there.
bool SeenByLastTrace(float3 world, out uint2 seen_pixel) {
  seen_pixel = 0u;
  const float4 clip = ClipOf(world, previous_view_projection);
  const float2 uv = clip.xy / max(clip.w, 1e-6f) * float2(0.5f, -0.5f) + 0.5f;
  if (clip.w <= 0.f || any(uv < 0.f) || any(uv >= 1.f)) return false;
  seen_pixel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u);
  const float4 seen = ambient_positions[seen_pixel];
  return seen.w > 0.f && distance(seen.xyz, world) < 0.03f + 0.01f * seen.w;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_CAMERA_HLSLI_
