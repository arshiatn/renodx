/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's props' liquid (the game's rigid_character_prop_liquid vs40_main, 0x766D159E) with its world position for ray
// tracing's stream output (rt_core.hpp): the instance's rows (POSITION1-3) x the vertex and the game's ripple (up to 1.4 cm
// up, by the time and the place, COLOR0.w its share), the game's TEXCOORD0. The game's inputs in its order.
cbuffer camera : register(b0)
{
  float time_in_sec : packoffset(c26);
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  float4 v12 : POSITION7,
  uint v13 : SV_InstanceID0,
  out float4 position : SV_Position0,
  out float4 world : TEXCOORD0)
{
  const float4 vertex = float4(v0.xyz, 1.f);
  world = float4(dot(vertex, v6), dot(vertex, v7), dot(vertex, v8), 1.f);
  const float2 waves = 0.5f * (1.f + sin(float2(dot(float2(0.707106769f, 0.707106769f), world.xz) * 50.f + 6.f * time_in_sec,
                                                 world.z * 100.f + 8.f * time_in_sec)));
  world.y += 0.5f * saturate(100.f * v5.w) * (waves.x * waves.x * 0.0125f + waves.y * waves.y * 0.0149999997f);
  position = 0.f;
}
