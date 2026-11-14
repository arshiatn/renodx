/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's rigid ropes (the game's rope vs40_main_rigid 0x1CEFE3BB and its shadow pass's vs40_depth_rigid 0x52D806A0: the
// same ribbon) with their world position for ray tracing's stream output (rt_core.hpp). The game's code: each instance's rows
// (POSITION1-3) place the rope's centre line, then the ribbon is TEXCOORD1.x x the rows' scale wide, across the rope as the
// draw's camera sees it, in that camera's view space. Its corner back in the world: the centre plus that offset through the
// view's rotation transposed (its rows orthonormal). The game's inputs in its order (its input layout binds by register).
cbuffer camera : register(b0) {
  float4 view_rows[4] : packoffset(c1);  // view, rows acting on column vectors
}

void main(
  float3 v0 : POSITION0,
  float3 v1 : NORMAL0,
  float2 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 v4 : TEXCOORD2,
  float4 v5 : POSITION1,
  float4 v6 : POSITION2,
  float4 v7 : POSITION3,
  float4 v8 : POSITION4,
  float4 v9 : POSITION5,
  float4 v10 : POSITION6,
  out float4 position : SV_Position0,
  out float4 world : TEXCOORD0)
{
  const float scale = length(float3(v5.x, v6.x, v7.x));
  const float3 along = normalize(float3(dot(normalize(v1), v5.xyz), dot(normalize(v1), v6.xyz), dot(normalize(v1), v7.xyz)));
  const float2 on_screen = normalize(float2(dot(along, view_rows[0].xyz), dot(along, view_rows[1].xyz)));
  const float2 across = normalize(float2(on_screen.y, -on_screen.x)) * v3.x * scale;
  const float3 centre = float3(dot(float4(v0, 1.f), v5), dot(float4(v0, 1.f), v6), dot(float4(v0, 1.f), v7));
  world = float4(centre + (across.x * view_rows[0].xyz) + (across.y * view_rows[1].xyz), 1.f);
  position = 0.f;
}
