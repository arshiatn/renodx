/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a placed variantrigid shadow caster's mesh (its first instance) in world space for the stream output, as the
// game's vertex shader places it (B0E006EE: dot(float4(v0, 1), v8-v10), its wind sway left out).
#include "./rt_rigid_shadow.hlsli"

void main(
  RT_VARIANT_SHADOW_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0)
{
  const float4 local = float4(v0, 1.f);
  capture_world = float4(dot(local, v8), dot(local, v9), dot(local, v10), 1.f);
  capture_position = 0.f;
}
