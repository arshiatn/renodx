/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's campaign trees (the game's rigidcampaigntrees vs40_main, 0xA8BB2DB2) with their world position for ray
// tracing's stream output (rt_core.hpp): the instance's rows (POSITION1-3) x the vertex, the game's TEXCOORD0. Solid:
// their G-buffer pixel shader D61D20DE clips nothing. The game's inputs in its order (its input layout binds by register).
void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : TEXCOORD1,
  float4 v7 : POSITION1,
  float4 v8 : POSITION2,
  float4 v9 : POSITION3,
  float4 v10 : POSITION4,
  float4 v11 : POSITION5,
  float4 v12 : POSITION6,
  out float4 position : SV_Position0,
  out float4 world : TEXCOORD0)
{
  world = float4(dot(v7, v0), dot(v8, v0), dot(v9, v0), 1.f);
  position = 0.f;
}
