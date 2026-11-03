/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smoke shadows without rays (smoke_map.hpp), first of a frame's passes: the map's view from this frame's sun and camera
// (their constants are on the GPU only). Around what the camera looks at (smoke_map_apply's last frame), held smoothly;
// on whole texels, so that moving the camera does not make the shadows swim.
#include "./smoke_map.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
}

cbuffer lighting_VS_PS : register(b1)
{
  float4 lighting_registers[4];
}

RWStructuredBuffer<SmokeView> view : register(u0);
RWStructuredBuffer<float4> anchor : register(u1);  // [0] what the map follows (w 1), [1] the screen's middle (w 0: sky)

float LightingFloat(uint index)
{
  return lighting_registers[index / 4u][index % 4u];
}

[numthreads(1, 1, 1)]
void main()
{
  const uint sun_index = (uint)g_smoke_sun_direction;
  const float3 sun_direction = float3(LightingFloat(sun_index), LightingFloat(sun_index + 1u), LightingFloat(sun_index + 2u));
  const float length_squared = dot(sun_direction, sun_direction);
  const float3 toward_sun = (length_squared > 0.f) ? -sun_direction * rsqrt(length_squared) : 0.f;
  const float4 looked_at = anchor[1];
  const float4 held = anchor[0];
  const float3 target = (looked_at.w != 0.f) ? looked_at.xyz : ((held.w != 0.f) ? held.xyz : camera_position);
  const float3 follows = (held.w != 0.f && distance(held.xyz, target) < 256.f) ? lerp(held.xyz, target, 0.05f) : target;
  anchor[0] = float4(follows, 1.f);
  SmokeView map = (SmokeView)0;
  // Above the horizon only: the heights count what lies higher up as nearer the sun.
  if (toward_sun.y > 0.049990736f) {
    map.coverage = SMOKE_MAP_COVERAGE;
    const float texel = map.coverage / SMOKE_MAP_SIZE;
    map.right = normalize(cross((abs(toward_sun.y) < 0.9900357f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f), toward_sun));
    map.up = cross(toward_sun, map.right);
    map.origin = floor(float2(dot(follows, map.right), dot(follows, map.up)) / texel + 0.5f) * texel;
    map.height = follows.y;
  }
  view[0] = map;
}
