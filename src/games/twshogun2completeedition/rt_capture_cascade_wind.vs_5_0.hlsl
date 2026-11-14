/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a wind-swayed rigid shadow caster (rigidvertexwind vs30_shadow, EDCD8445: placed by its instance's rows
// v6-v8, swayed by COLOR0.w) for the outside-view geometry shader (twpharaohdynasties RT_WORLD: TEXCOORD0 the world): the game's
// corner in world space with its sway, as its vertex shader works them out (its pixel shader 237253A3 tests nothing: solid).
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
  float time_in_sec : packoffset(c35);
}

cbuffer wind_VS : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
}

struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
};

Corner main(float4 v0 : POSITION0, float4 v5 : COLOR0, float4 v6 : POSITION1, float4 v7 : POSITION2, float4 v8 : POSITION3)
{
  const float4 local = float4(v0.xyz, 1.f);
  const float height = dot(local, v7);
  // A flutter by the corner's x, two waves along the wind by its placed height and a drift with it, all by COLOR0.w.
  const float phase = (time_in_sec * 10.f) + v0.x;
  const float3 flutter = float3(0.02f, 0.07f, 0.03f) * ((sin(phase) - 0.3f) * sin(0.8f * phase));
  const float2 waves = sin((float2(10.f, 5.f) * time_in_sec) + (height * float2(10.f, 100.f)));
  const float2 along = g_wind_direction * v5.w
                       * ((0.1f * waves.x * waves.y) + (0.35f * ((sin((time_in_sec * 20.f) + height) * 0.01f) + 0.01f)));
  Corner corner;
  corner.world = float4(float3(dot(local, v6), height, dot(local, v8)) + (flutter * v5.w) + float3(along.x, 0.f, along.y), 1.f);
  corner.position = mul(corner.world, view_projection);
  return corner;
}
