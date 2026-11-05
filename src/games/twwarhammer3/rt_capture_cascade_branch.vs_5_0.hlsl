/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a tree's branches first met in the shadow pass (D75E6E2B, 8F975E16), its first instance in world space.
#include "./rt_cascade.hlsli"

void main(
  RT_TREE_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0) {
  capture_world = float4(RT_TREE_REST_WORLD, 1.f);
  capture_position = 0.f;
}
