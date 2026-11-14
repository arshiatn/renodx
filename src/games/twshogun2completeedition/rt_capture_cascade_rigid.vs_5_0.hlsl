/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a placed rigid shadow caster's mesh (its first instance) in world space for the stream output, as the game's
// vertex shader places it (AC25DAB8: dot(float4(v0.xyz, 1), v6-v8)).
#include "./rt_rigid_shadow.hlsli"

void main(
  RT_RIGID_SHADOW_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0)
{
  const float4 local = float4(v0.xyz, 1.f);
  capture_world = float4(dot(local, v6), dot(local, v7), dot(local, v8), 1.f);
  capture_position = 0.f;
}
