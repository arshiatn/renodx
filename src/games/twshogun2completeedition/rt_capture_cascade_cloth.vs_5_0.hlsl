/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a cloth shadow caster (sails, banners: 8A00A1D4, whose vertices are in world space already) for the
// outside-view geometry shader: its world position and the UV of its pixel shader's mask test (5F7C4EB5's t0).
#include "./rt_cascade_leaf.hlsli"

RtCascadeLeaf main(float4 v0 : POSITION0, float4 v2 : TEXCOORD1) {
  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = v2.xy;
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = float4(v0.xyz / v0.w, 1.f);
  return leaf;
}
