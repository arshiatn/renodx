/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced reflections (ray_tracing.hpp) into the IBL specular that DeferredSky and the light probes accumulated, before
// ClusteredLightingWithSSAO reads it (its t7), blended one / inverse source alpha (RGB): where the rays met a surface
// (t5's alpha, the share of samples) their light times the surface's specular colour as DeferredSky's IBL takes it
// (rt_ibl.hlsli: F0 x the LUT's scale + its bias) replaces the game's specular; the game's stays for the rest (the sky, the
// rougher surfaces the rays leave), times the open share of the lobe (t6, Ray-Traced Specular Occlusion; unbound: 1). The
// far plane and unlit pixels keep the game's (0 there). t5 unbound (the reflections off): the occlusion alone.
#include "./rt_ibl.hlsli"

Texture2D<float4> t_reflection : register(t5);  // the rays' light, the share of samples that met a surface
Texture2D<float> t_open : register(t6);         // the share of the lobe open (1: nothing in the way)

float4 main(float4 position : SV_Position) : SV_Target {
  const uint2 pixel = uint2(position.xy);
  const IblSurface surface = ReadIblSurface(pixel);
  if (!surface.lit) return 0.f;
  const float4 traced = t_reflection.Load(int3(pixel, 0));
  uint width, height;
  t_open.GetDimensions(width, height);
  const float open = (width != 0u) ? t_open.Load(int3(pixel, 0)) : 1.f;
  return float4(surface.specular * traced.rgb * traced.a, 1.f - (1.f - traced.a) * open);
}
