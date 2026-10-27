/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_RT_AMBIENT_HLSLI_
#define SRC_GAMES_TALESOFARISE_RT_AMBIENT_HLSLI_

// Ray-traced ambient occlusion, bounce light (or path tracing), reflections and specular occlusion (the DLSS addon's
// Experimental options, rt_shadows.hpp), bound only while they run (the render size): the ambient visibility at t41
// (1 open, 0 occluded), multiplied into the lighting pass's ambient occlusion; the bounce light at t42 (the light
// arriving from nearby surfaces, times its strength); the reflections at t43 (the light the pixel's own reflection
// lobe finds, the share of its rays that met a surface); the specular occlusion at t44 (the share of the lobe open).
Texture2D<float> t41 : register(t41);
Texture2D<float4> t42 : register(t42);
Texture2D<float4> t43 : register(t43);
Texture2D<float> t44 : register(t44);

float RayTracedAmbientOcclusion(float2 uv) {
  uint width, height;
  t41.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  return t41.Load(int3(min(uint2(uv * float2(width, height)), uint2(width, height) - 1u), 0));
}

// The bounce light a surface of `base_color` sends on (Lambert): 0 when it is off.
float3 RayTracedBounceLight(float2 uv, float3 base_color) {
  uint width, height;
  t42.GetDimensions(width, height);
  if (width == 0u) return 0.f;
  return base_color * t42.Load(int3(min(uint2(uv * float2(width, height)), uint2(width, height) - 1u), 0)).rgb;
}

// The game's screen reflection sample (`game`: rgb, coverage) with the ray-traced reflection where its rays met a
// surface (the rays sample the pixel's own roughness): unchanged when off.
float4 RayTracedReflection(float2 uv, float4 game) {
  uint width, height;
  t43.GetDimensions(width, height);
  if (width == 0u) return game;
  const float4 traced = t43.Load(int3(min(uint2(uv * float2(width, height)), uint2(width, height) - 1u), 0));
  return lerp(game, float4(traced.rgb, 1.f), saturate(traced.a));
}

// The share of the pixel's reflection lobe open to the sky: 1 when off.
float RayTracedSpecularOcclusion(float2 uv) {
  uint width, height;
  t44.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  return t44.Load(int3(min(uint2(uv * float2(width, height)), uint2(width, height) - 1u), 0));
}

#endif  // SRC_GAMES_TALESOFARISE_RT_AMBIENT_HLSLI_
