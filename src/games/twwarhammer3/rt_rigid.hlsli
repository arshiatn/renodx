/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing (twattila/rt_scene.hpp): the rigid objects' vertex input (F0395CC4, 4F35D09F, 41AC9CB1, 33AF7AE5: the same signature and
// position math by their bytes: rows v6-v8, the position scaled by v0.w at least 1).
#define RT_RIGID_INPUTS \
  float4 v0 : POSITION0, \
  float4 v1 : TEXCOORD0, \
  float4 v2 : NORMAL0, \
  float4 v3 : NORMAL1, \
  float4 v4 : NORMAL2, \
  float4 v5 : COLOR0, \
  float4 v6 : POSITION1, \
  float4 v7 : POSITION2, \
  float4 v8 : POSITION3, \
  float4 v9 : POSITION4, \
  float4 v10 : POSITION5, \
  float4 v11 : POSITION6, \
  float4 v12 : POSITION7, \
  uint2 v13 : POSITION8, \
  float v14 : POSITION9, \
  uint v15 : SV_InstanceID0, \
  uint v16 : SV_VertexID0, \


float3 RtRigidWorld(float4 position, float4 row0, float4 row1, float4 row2) {
  const float4 scaled = float4(position.xyz * max(position.w, 1.f), 1.f);
  return float3(dot(scaled, row0), dot(scaled, row1), dot(scaled, row2));
}
