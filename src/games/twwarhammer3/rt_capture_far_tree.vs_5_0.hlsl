/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// fx_rigidtreebillboard 01830D95 (rt_far_tree.hlsli).
#include "./rt_far_tree.hlsli"

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float v4 : TEXCOORD3,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0) {
  world_uv = FarTreeWorldUv(v0.xy, v1, v2, v3);
  position = 0.f;
}
