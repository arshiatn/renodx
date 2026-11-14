/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Through Foliage (foliage_light.hpp; Attila's sss_depth): the depth Shogun 2's screen-space shadows march (its
// depth buffer itself, D24) into a single-channel texture, foliage's pixels negative (the G-buffer constants' alpha byte
// & 128: the sun light shaders' foliage bit): Bend's march (bend_sss_gpu.hlsli CasterLight) lets that share of the
// sun through what they shadow.
Texture2D<float> t_depth : register(t0);
Texture2D<float4> t_constants : register(t1);
RWTexture2D<float> u_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_depth.GetDimensions(width, height);
  if (any(id.xy >= uint2(width, height))) return;
  const float depth = t_depth[id.xy];
  const bool foliage = (((uint)round(t_constants[id.xy].w * 255.f)) & 128u) != 0u;
  u_depth[id.xy] = foliage ? -depth : depth;
}
