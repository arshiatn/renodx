/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Global Illumination through the rays (ray_tracing.hpp): the resolved bounce added to the scene before DeferredFog
// (additive blend, RGB), Divinity 2's rt_bounce_apply on this game's G-buffer. Each pixel that isn't the sky (linear
// depth below 0.999, as d1_rt_inputs) gathers the bounce texels around it (3 x 3, by distance, by how near their surface
// lies to its plane and by their normals), times its diffuse colour as the sun pass's ambient takes it (gBuffer3.rgb,
// DeferredDirectionalWithSSAO) and the strength. Normals: gBuffer2's stereographic view-space normal turned to the world
// by the view's rotation transposed (the sun pass's invViewMatrix).
Texture2D<float4> resolved : register(t0);            // the light arriving per bounce texel (scale x scale render pixels)
Texture2D<float4> resolved_positions : register(t1);  // their surfaces (w: distance, 0 the sky)
Texture2D<float4> positions : register(t2);           // the render pixels' (d1_rt_inputs)
Texture2D<float2> normals : register(t3);             // gBuffer2: the view-space normal, stereographic / 1.7777
Texture2D<float4> diffuse : register(t4);             // gBuffer3: the diffuse colour
Texture2D<float> linear_depth : register(t5);         // gBuffer1: view z / far, 1 the sky

cbuffer apply : register(b0) {
  uint4 sizes;      // xy the bounce texels, zw the render size
  float4 settings;  // x the strength, y the render pixels per bounce texel each way (1, 2 or 4)
  float4 view[3];   // the View's rows (its rotation in xyz)
};

float3 WorldNormal(int2 pixel) {
  const float3 encoded = float3(normals.Load(int3(pixel, 0)) * 3.5554f - 1.7777f, 1.f);
  const float g = 2.f / dot(encoded, encoded);
  const float3 normal = float3(g * encoded.xy, g - 1.f);
  return normalize(normal.x * view[0].xyz + normal.y * view[1].xyz + normal.z * view[2].xyz);
}

float4 main(float4 sv_position : SV_Position) : SV_Target {
  const int2 pixel = int2(sv_position.xy);
  const float4 position = positions.Load(int3(pixel, 0));
  if (!(linear_depth.Load(int3(pixel, 0)) < 0.9989119f) || position.w <= 0.f) return 0.f;
  const float3 normal = WorldNormal(pixel);
  const int scale = int(settings.y);
  const float2 centre = (float2(pixel) + .5f) / float(scale) - .5f;
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
      const float3 there_normal = WorldNormal(min(at * scale + (scale >> 1), int2(sizes.zw) - 1));
      const float weight = exp(-dot(offset, offset)) * exp(-plane * plane) * pow(saturate(dot(normal, there_normal)), 8.f);
      light += resolved.Load(int3(at, 0)).rgb * weight;
      weights += weight;
    }
  }
  if (weights <= 1.0001435e-4f) return 0.f;
  return float4(light / weights * diffuse.Load(int3(pixel, 0)).rgb * settings.x, 0.f);
}
