/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: ray-traced reflections in place
// of Unreal's screen-space reflections (their draw is left out), written as the reflection pass reads that target: the
// rays' light where they met something (premultiplied) and that share in alpha; the reflection captures and the sky
// fill the rest.
Texture2D<float4> reflection : register(t0);  // light, the share of samples that met a surface (the render pixels)

float4 main(float4 position : SV_Position) : SV_Target {
  const float4 traced = reflection.Load(int3(position.xy, 0));
  return float4(traced.rgb * traced.a, traced.a);
}
