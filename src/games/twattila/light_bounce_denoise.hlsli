/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), the denoiser (light_bounce.hpp; the 16-tap edge-aware idea of three-gtvbao's denoiser,
// written here): 16 texels on a disk of 2.5 texels around this one (a golden-angle spiral, turned every frame with
// history), each weighted by its distance, by how far it lies from this texel's plane and by how alike its normal is,
// so the bounce smooths along surfaces and never across edges. Into the texture the apply reads (the history keeps the
// resolve's). Directional Light Bounce (LB.directional, bitmask_sh.hlsli): the resolve's (g, w) and zonal amplitude
// (t56-t57) with the same weights into SV_Target1-2, w pinned at this texel's normal last; each tap's light first moved
// LB.transport of the way toward its own value at this texel's normal (0: the blur of the light across normals, unbiased
// but softer; 1: each tap's value there, sharper, darker; shg_ S0 12.2). Game-neutral: the includer's
// light_bounce.hlsli first.
#include "./bitmask_sh.hlsli"
#include "./light_bounce.h"
#include "./light_bounce_geometry.hlsli"

Texture2D<float4> t_resolved : register(t40);  // the resolve's output (alpha: its distance, 0: nothing)
Texture2D<float4> t_resolved_sh : register(t56);  // the resolve's (g, w)
Texture2D<float> t_resolved_zonal : register(t57);  // the resolve's zonal amplitude

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0
#if defined(LIGHT_BOUNCE_DIRECTIONAL)
          , out float4 o1 : SV_Target1, out float o2 : SV_Target2) {
#else
          ) {
  float4 o1;
  float o2;
#endif
  const int2 texel = int2(v0.xy);
  const float4 center = t_resolved.Load(int3(texel, 0));
  o0 = center;
  o1 = 0.f;
  o2 = 0.f;
  const bool directional = BOUNCE_DIRECTIONAL;
  [branch]
  if (directional) {
    o1 = t_resolved_sh.Load(int3(texel, 0));
    o2 = t_resolved_zonal.Load(int3(texel, 0));
  }
  if (!(center.a > 0.f)) return;
  const BounceGeometry geometry = LoadGeometry(texel, 0u, 0u);
  // Off this texel's plane by 2% of its distance: another surface.
  const float tolerance = 0.02f * center.a;
  const float turn = frac(LB.frame * 0.618034f) * 6.2831853f;
  // Directional: the trace's normal here, this texel's light.
  const float3 normal = BitmaskShFacing(geometry.normal, -geometry.position / length(geometry.position));
  const float center_y = dot(center.rgb, BITMASK_SH_LUMINANCE);
  float4 sum = float4(center.rgb, 1.f);
  float4 sh_sum = o1;
  float zonal_sum = o2;
  [unroll]
  for (int tap = 0; tap < 16; ++tap) {
    const float radius = 2.5f * sqrt((tap + 0.5f) / 16.f);
    const float angle = tap * 2.3999632f + turn;
    const int2 other_texel = texel + int2(round(radius * float2(cos(angle), sin(angle))));
    const float4 other = t_resolved.Load(int3(other_texel, 0));
    if (!(other.a > 0.f)) continue;
    const BounceGeometry other_geometry = LoadGeometry(other_texel, 0u, 0u);
    const float plane = saturate(1.f - abs(dot(geometry.normal, other_geometry.position - geometry.position)) / tolerance);
    const float facing = pow(saturate(dot(geometry.normal, other_geometry.normal)), 8.f);
    const float near = exp2(-0.25f * radius * radius);
    const float weight = near * plane * facing;
    float3 light = other.rgb;
    [branch]
    if (directional) {
      const float4 other_sh = t_resolved_sh.Load(int3(other_texel, 0));
      const float other_zonal = t_resolved_zonal.Load(int3(other_texel, 0));
      const float other_y = dot(other.rgb, BITMASK_SH_LUMINANCE);
      // Its gain toward this normal as the apply's (0-4, against 1% of the brighter of the two), LB.transport of it.
      const float gain = BitmaskShEvaluate(other_y, other_sh, other_zonal, normal) / max(other_y, 0.01f * max(center_y, other_y) + 1e-6f);
      light *= 1.f + LB.transport * (clamp(gain, 0.f, 4.f) - 1.f);
      sh_sum += other_sh * weight;
      zonal_sum += other_zonal * weight;
    }
    sum += float4(light, 1.f) * weight;
  }
  o0.rgb = sum.rgb / sum.a;
  [branch]
  if (directional) {
    const BitmaskShLight pinned = BitmaskShPinned(sh_sum.xyz / sum.a, zonal_sum / sum.a, normal);
    o1 = float4(pinned.g, pinned.w);
    o2 = pinned.zonal;
  }
}
