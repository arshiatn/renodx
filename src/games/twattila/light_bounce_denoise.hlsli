/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), the denoiser (light_bounce.hpp; the 16-tap edge-aware idea of three-gtvbao's denoiser,
// written here): 16 texels on a disk of 2.5 texels around this one (a golden-angle spiral, turned every frame with
// history), each weighted by its distance, by how far it lies from this texel's plane and by how alike its normal is,
// so the bounce smooths along surfaces and never across edges. Into the texture the apply reads (the history keeps the
// resolve's). Game-neutral: the includer's light_bounce.hlsli first.
#include "./light_bounce.h"
#include "./light_bounce_geometry.hlsli"

Texture2D<float4> t_resolved : register(t40);  // the resolve's output (alpha: its distance, 0: nothing)

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0) {
  const int2 texel = int2(v0.xy);
  const float4 center = t_resolved.Load(int3(texel, 0));
  o0 = center;
  if (!(center.a > 0.f)) return;
  const BounceGeometry geometry = LoadGeometry(texel, 0u, 0u);
  // Off this texel's plane by 2% of its distance: another surface.
  const float tolerance = 0.02f * center.a;
  const float turn = frac(LB.frame * 0.618034f) * 6.2831853f;
  float4 sum = float4(center.rgb, 1.f);
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
    sum += float4(other.rgb, 1.f) * (near * plane * facing);
  }
  o0.rgb = sum.rgb / sum.a;
}
