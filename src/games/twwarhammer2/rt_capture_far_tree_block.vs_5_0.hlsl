/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer II's far tree blocks (rigidtreebillboard vs40_main_block, 0x62C6A599): vs40_main's code (0xC1A857A3) with its
// inputs at POSITION1-3 and no TEXCOORD3: the same capture (rt_capture_far_tree.vs_5_0.hlsl).
#define RT_FAR_TREE_NO_CONSTANTS
#define main FarTreeCard
#include "../twpharaohdynasties/rt_capture_far_tree.vs_5_0.hlsl"
#undef main

void main(
  float4 v0 : POSITION0,
  float2 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0) {
  FarTreeCard(v0, float4(v1, 0.f, 0.f), v2, v3, 0.f, position, world_uv);
}
