/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced specular occlusion without the traced reflections (ray_tracing.hpp BindReflections): merged into the
// game's screen-space reflections for its tiled lighting (cs 0x897232B5, its t6: specular = lerp(probes, t6.rgb,
// t6.a)). The probes' share times the occlusion, lerp(probes x occlusion, rgb, a), written as lerp(probes, rgb', a'):
// a' = 1 - occlusion x (1 - a), rgb' = rgb x a / a'. Open (1): the game's as it was; unbound t6 (its reflections off):
// the probes x the occlusion.
Texture2D<float4> t_reflections : register(t0);  // the game's t6 (half the render size), sampled as the lighting does
Texture2D<float> t_occlusion : register(t1);     // 1 open; the render size, the bridge's top-left
SamplerState s_linear : register(s0);
RWTexture2D<float4> u_merged : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_merged.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  const float4 reflected = t_reflections.SampleLevel(s_linear, (float2(id.xy) + 0.5f) / float2(width, height), 0);
  const float share = saturate(reflected.a);
  const float merged_share = 1.f - saturate(t_occlusion[id.xy]) * (1.f - share);
  u_merged[id.xy] = float4(reflected.rgb * ((merged_share > 0.f) ? share / merged_share : 0.f), merged_share);
}
