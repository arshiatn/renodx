/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: ray-traced reflections in place
// of Unreal's screen-space reflections (their draw is left out), written as the reflection pass reads that target: the
// rays' light where they met something (premultiplied) and that share in alpha; the reflection captures and the sky
// fill the rest (1 - alpha), with Specular Occlusion only where the lobe is open. Specular Occlusion alone (t0 unbound:
// 0): after the game's own screen-space reflections, alpha only, blended as a + (1 - a) x this alpha, so the captures and
// the sky keep (1 - a) x open.
Texture2D<float4> reflection : register(t0);  // light, the share of samples that met a surface (the render pixels)
Texture2D<float> open_lobe : register(t1);    // the share of the specular lobe open (Specular Occlusion; unbound: open)

float4 main(float4 position : SV_Position) : SV_Target {
  const float4 traced = reflection.Load(int3(position.xy, 0));
  uint width = 0u;
  uint height = 0u;
  open_lobe.GetDimensions(width, height);
  const float open = (width != 0u) ? open_lobe.Load(int3(position.xy, 0)) : 1.f;
  return float4(traced.rgb * traced.a, 1.f - (1.f - traced.a) * open);
}
