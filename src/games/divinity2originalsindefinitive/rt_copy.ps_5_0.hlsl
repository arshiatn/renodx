/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced reflections (ray_tracing.hpp): DeferredSSRCombine's input scene (its t0) written into its target as it is, in
// its place (no screen-space reflection over the rays'), with the full-screen triangle light_bounce_vs.
Texture2D<float4> scene : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
  return float4(scene.Load(int3(position.xy, 0)).rgb, 1.f);
}
