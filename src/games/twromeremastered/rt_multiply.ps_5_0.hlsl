/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing (ray_tracing.hpp): the traced visibility (t0, R8 at the render size, 1 open) for a multiply blend into the
// game's target (its red: the shadow mask's sun visibility, ASSAO's occlusion), with the full-screen triangle
// light_bounce_vs.
Texture2D<float> visibility : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
  return visibility.Load(int3(position.xy, 0)).xxxx;
}
