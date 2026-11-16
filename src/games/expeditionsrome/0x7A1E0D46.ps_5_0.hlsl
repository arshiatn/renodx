/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: ray-traced ambient occlusion into
// Unreal's screen-space AO before the base pass AO (192686A0, traced at Unreal's AO passes) or the sun's deferred light
// draw (the sky and reflection pass reads it after), in its place (no blend: the AO's R8 holds the traced alone).
Texture2D<float> ambient : register(t0);  // traced, 1 open (the render pixels)

float4 main(float4 position : SV_Position) : SV_Target {
  return ambient.Load(int3(position.xy, 0)).xxxx;
}
