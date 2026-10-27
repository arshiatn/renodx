/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced reflections and specular occlusion in the games' reflection passes (rt_scene.hpp BindReflections; the
// helper's rt_path.hlsli traces them at the render pixels): t47 the rays' light and the share of samples that met a
// surface (bound only with Ray-Traced Reflections on), t46 the share of the lobe open (bound with either on). Where the
// rays met something their light replaces the screen-space march; the game's environment stays for the rest (the sky).
#ifndef SRC_GAMES_TWATTILA_RT_REFLECTION_HLSLI_
#define SRC_GAMES_TWATTILA_RT_REFLECTION_HLSLI_

Texture2D<float4> t_rt_reflection : register(t47);
Texture2D<float> t_rt_specular_occlusion : register(t46);

bool RtReflectionsBound() {
  uint width, height;
  t_rt_reflection.GetDimensions(width, height);
  return width != 0u;
}

// The environment where the lobe is open (1 when the occlusion is not bound).
float3 RtOccludedEnvironment(float2 render_pixel, float3 environment) {
  uint width, height;
  t_rt_specular_occlusion.GetDimensions(width, height);
  return (width != 0u) ? environment * t_rt_specular_occlusion.Load(int3(render_pixel, 0)) : environment;
}

// The reflection at a render pixel: the rays' light for the share that met a surface, `environment` for the rest.
float3 RtReflection(float2 render_pixel, float3 environment) {
  const float4 traced = t_rt_reflection.Load(int3(render_pixel, 0));
  return traced.rgb * traced.a + environment * (1.f - traced.a);
}

#endif  // SRC_GAMES_TWATTILA_RT_REFLECTION_HLSLI_
