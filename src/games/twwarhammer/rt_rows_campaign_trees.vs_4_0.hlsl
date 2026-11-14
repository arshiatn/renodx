/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The campaign trees' (0xA8BB2DB2) instance rows for ray tracing's placed draws (rt_core.hpp, SceneShader::rows_shader):
// their per-instance POSITION1-3, the rows the game multiplies the vertex by. The game's inputs in its order.
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
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out uint4 record : TEXCOORD3)
{
  row0 = v7;
  row1 = v8;
  row2 = v9;
  record = 0u;
}
