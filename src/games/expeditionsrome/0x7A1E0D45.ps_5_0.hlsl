/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: ray-traced sun shadows into
// Unreal's light attenuation before the sun's deferred light draw. The sun's shadowing comes out as the darker of the
// game's (its whole-scene shadows faded toward the static ones with distance, as the light shader computes them) and
// the traced visibility: the light function / per-object channel (z, stored as a square root, multiplied into the
// surface and the subsurface shadows) is scaled by sqrt(min(1, traced / game's)); the other channels stay (multiply
// blend, blue only). Its View and light constants are the light draw's own (b0, b1).
Texture2D<float4> attenuation : register(t0);  // a copy: x whole-scene shadow, z light function x per-object (square roots)
Texture2D<float> depth : register(t1);         // device Z
Texture2D<float4> gbuffer_b : register(t2);    // a: shading model | flags << 4 (/ 255)
Texture2D<float4> gbuffer_e : register(t3);    // precomputed shadow factors
Texture2D<float> visibility : register(t4);    // traced, 1 lit (the render pixels)

cbuffer View : register(b0) {
  float4 view[62];  // [61] InvDeviceZToWorldZTransform
};

cbuffer DeferredLightUniforms : register(b1) {
  float4 shadow_map_channel_mask;
  float4 distance_fade;  // xy DistanceFadeMAD
};

float4 main(float4 position : SV_Position) : SV_Target {
  const int3 pixel = int3(position.xy, 0);
  const float stored = attenuation.Load(pixel).x;
  // The static shadowing: GBufferE unless the flags skip it (bit 1: none, bit 2 with it: zero).
  const uint flags = uint(round(gbuffer_b.Load(pixel).a * 255.f));
  float4 precomputed = gbuffer_e.Load(pixel);
  if ((flags & 32u) != 0u) precomputed = ((flags & 64u) != 0u) ? 0.f : 1.f;
  const float static_shadowing = lerp(1.f, dot(precomputed, shadow_map_channel_mask), dot(shadow_map_channel_mask, 1.f));
  // The dynamic shadows fade toward it with the pixel's depth.
  const float device_z = depth.Load(pixel);
  const float4 transform = view[61];
  const float scene_depth = device_z * transform.x + transform.y + 1.f / (device_z * transform.z - transform.w);
  const float fade = saturate(scene_depth * distance_fade.x + distance_fade.y);
  const float game = lerp(stored * stored, static_shadowing, fade * fade);
  return float4(1.f, 1.f, sqrt(saturate(visibility.Load(pixel) / max(game, 1e-4f))), 1.f);
}
