/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: the ray-traced light bounce's
// prepared light, the light leaving what the camera sees (the scene colour right after the sun's deferred light draw),
// one texel per 2 x 2 render pixels (their average; non-finite ones left out).
Texture2D<float4> scene : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
  const int2 pixel = int2(position.xy) * 2;
  float3 light = 0.f;
  [unroll]
  for (int sample = 0; sample < 4; ++sample) {
    const float3 color = scene.Load(int3(pixel + int2(sample & 1, sample >> 1), 0)).rgb;
    light += all(isfinite(color)) ? max(color, 0.f) : 0.f;
  }
  return float4(light * .25f, 1.f);
}
