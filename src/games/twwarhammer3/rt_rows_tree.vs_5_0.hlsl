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
  // The second rows (v11-v13, which the game blends in by each vertex's v6.x) against these, for the log (rt_core's second rows line).
  const float3x3 turned = float3x3(v11.xyz - v8.xyz, v12.xyz - v9.xyz, v13.xyz - v10.xyz);
  record = float4(sqrt(dot(turned[0], turned[0]) + dot(turned[1], turned[1]) + dot(turned[2], turned[2])) / max(length(float3(v8.x, v9.x, v10.x)), 1e-6f),
                  length(float3(v11.w - v8.w, v12.w - v9.w, v13.w - v10.w)), 0.f, 0.f);
}
