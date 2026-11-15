/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced ground shadows (ray_tracing.hpp): the traced sun visibility into the ground's shadow mask (the pre-pass
// target's copy, its red read by the terrain and water shaders by screen position). The pass blends by minimum into red
// alone: the darker of the game's shadow and the rays'.
sampler2D visibility : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
  return tex2Dlod(visibility, float4(uv, 0.f, 0.f)).x;
}
