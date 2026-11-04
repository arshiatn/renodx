/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a placed tree draw's instance rows (fx_rigidleaf_v3's 783383C6, fx_rigidbranch_v3's 474F97FB: one signature, rows v8-v10), one point an
// instance, for the stream output.
void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float2 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : POSITION7,
  float4 v8 : TEXCOORD0,
  float4 v9 : TEXCOORD1,
  float4 v10 : TEXCOORD2,
  float4 v11 : TEXCOORD3,
  float4 v12 : TEXCOORD4,
  float4 v13 : TEXCOORD5,
  float4 v14 : TEXCOORD6,
  float4 v15 : TEXCOORD7,
  uint v16 : SV_InstanceID0,
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out float4 record : TEXCOORD3)
{
  row0 = v8;
  row1 = v9;
  row2 = v10;
  record = 0.f;
}
