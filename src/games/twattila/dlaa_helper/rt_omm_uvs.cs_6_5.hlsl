/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// GPU Micromap Baking (rt_d3d12.hpp BakeMicromapsOnGpu): a bake's UVs (a packed half UV a corner from `uvs`, its place
// bound) copied for the OMM SDK's GPU baker with every coordinate past UV_LIMIT texture widths, or not finite, at 0. The
// bake goes through every texel a micro-triangle covers: a garbage UV (some of the game's captures carry them) made one
// run for seconds (the GPU resets of ATN's game, 2026-10-28, when micromaps were baked on the GPU before).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_omm_uvs.h -Vn RT_OMM_UVS.

ByteAddressBuffer uvs : register(t0);
RWByteAddressBuffer clamped : register(u6);

cbuffer Dispatch : register(b1) {
  uint corners;
  uint3 padding;
};

static const float UV_LIMIT = 2.f;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= corners) return;
  const uint packed = uvs.Load(id.x * 4u);
  const float2 uv = float2(f16tof32(packed), f16tof32(packed >> 16));
  // A NaN fails the comparison too.
  const float2 kept = all(abs(uv) <= UV_LIMIT) ? uv : 0.f;
  clamped.Store(id.x * 4u, f32tof16(kept.x) | (f32tof16(kept.y) << 16));
}
