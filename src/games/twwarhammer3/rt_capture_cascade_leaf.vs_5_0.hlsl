/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a tree's leaves first met in the shadow pass (AF3F74B2, 2327ED2B), its first instance in world space, the mask's UV at v5.
#include "./rt_cascade.hlsli"
#include "../twattila/rt_capture.hlsli"

void main(
  RT_TREE_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0) {
  capture_world = RtWorldUv(RT_TREE_REST_WORLD, v5.xy);
  capture_position = 0.f;
}
