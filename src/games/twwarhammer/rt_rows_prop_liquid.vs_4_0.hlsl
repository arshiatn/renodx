/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The props' liquid's (0x766D159E) instance rows for ray tracing's placed draws (rt_core.hpp, SceneShader::rows_shader):
// its per-instance POSITION1-3 (its ripple, under 1.4 cm, left out). The game's inputs in its order.
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
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out uint4 record : TEXCOORD3)
{
  row0 = v6;
  row1 = v7;
  row2 = v8;
  record = 0u;
}
