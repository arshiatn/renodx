/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: the ray-traced light bounce's
// prepared light, the light leaving what the camera sees (the scene colour right after the sun's deferred light draw),
// one texel per scale x scale render pixels (their average; non-finite ones left out).
Texture2D<float4> scene : register(t0);

cbuffer prepare : register(b0) {
  uint4 sizes;      // xy the bounce texels, zw the render size
  float4 settings;  // y the render pixels per texel each way (1, 2 or 4)
};

float4 main(float4 position : SV_Position) : SV_Target {
  const int scale = int(settings.y);
  const int2 pixel = int2(position.xy) * scale;
  float3 light = 0.f;
  for (int y = 0; y < scale; ++y) {
    for (int x = 0; x < scale; ++x) {
      const float3 color = scene.Load(int3(pixel + int2(x, y), 0)).rgb;
      light += all(isfinite(color)) ? max(color, 0.f) : 0.f;
    }
  }
  return float4(light / float(scale * scale), 1.f);
}
