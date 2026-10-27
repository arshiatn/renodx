/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: the ray-traced light bounce added
// to the scene colour right after the sun's deferred light draw (additive blend, RGB). Each lit render pixel gathers
// the resolved bounce texels around it (3 x 3, weighted by distance, by how near their surface lies to its plane and by
// their normals), times its diffuse colour (base colour x (1 - metallic)) and the strength.
Texture2D<float4> resolved : register(t0);            // the light arriving per bounce texel (2 x 2 render pixels)
Texture2D<float4> resolved_positions : register(t1);  // their surfaces (metres; w: distance, 0 the sky)
Texture2D<float4> positions : register(t2);           // the render pixels' (the helper's)
Texture2D<float4> gbuffer_a : register(t3);           // world normal x 0.5 + 0.5
Texture2D<float4> gbuffer_b : register(t4);           // metallic, specular, roughness, shading model | flags << 4 (/ 255)
Texture2D<float4> gbuffer_c : register(t5);           // base colour (linear)

cbuffer apply : register(b0) {
  uint4 sizes;      // xy the bounce texels, zw the render size
  float4 settings;  // x the strength
};

float4 main(float4 sv_position : SV_Position) : SV_Target {
  const int2 pixel = int2(sv_position.xy);
  const float4 surface = gbuffer_b.Load(int3(pixel, 0));
  const float4 position = positions.Load(int3(pixel, 0));
  if ((uint(round(surface.a * 255.f)) & 15u) == 0u || position.w <= 0.f) return 0.f;
  const float3 normal = normalize(gbuffer_a.Load(int3(pixel, 0)).xyz * 2.f - 1.f);
  const float2 centre = (float2(pixel) + .5f) * .5f - .5f;
  const int2 base = int2(round(centre));
  float3 light = 0.f;
  float weights = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const int2 at = base + int2(x, y);
      if (any(at < 0) || any(at >= int2(sizes.xy))) continue;
      const float4 there = resolved_positions.Load(int3(at, 0));
      if (there.w <= 0.f) continue;
      const float2 offset = float2(at) - centre;
      const float plane = abs(dot(normal, there.xyz - position.xyz)) / max(.01f * position.w, .02f);
      const float3 there_normal = gbuffer_a.Load(int3(min(at * 2 + 1, int2(sizes.zw) - 1), 0)).xyz * 2.f - 1.f;
      const float weight = exp(-dot(offset, offset)) * exp(-plane * plane) * pow(saturate(dot(normal, normalize(there_normal))), 8.f);
      light += resolved.Load(int3(at, 0)).rgb * weight;
      weights += weight;
    }
  }
  if (weights <= 1e-4f) return 0.f;
  const float3 diffuse = gbuffer_c.Load(int3(pixel, 0)).rgb * (1.f - surface.r);
  return float4(light / weights * diffuse * settings.x, 0.f);
}
