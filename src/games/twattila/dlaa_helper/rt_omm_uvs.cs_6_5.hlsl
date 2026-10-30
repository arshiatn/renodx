/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// GPU Micromap Baking (rt_d3d12.hpp BakeMicromapsOnGpu), a triangle a thread. Before the OMM SDK's bake: a bake's UVs (a
// packed half UV a corner from `source`, its place bound) copied to `target` for the SDK's GPU baker. The bake goes through
// every texel a micro-triangle covers: garbage UVs (some of the game's captures carry them) made bakes run for seconds
// (the GPU resets of ATN's game, 2026-10-28 and debug30). A triangle with a coordinate not finite or past UV_LIMIT texture
// widths, or reaching over more than TRIANGLE_REACH of them, goes to COLLAPSED (no texels to go through). After the bake
// (`indices`): `source` the bake's UVs, `target` its micromap indices; a collapsed triangle's becomes unknown opaque (the
// rays' alpha test decides there).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_omm_uvs.h -Vn RT_OMM_UVS.

ByteAddressBuffer source : register(t0);
RWByteAddressBuffer target : register(u6);

cbuffer Dispatch : register(b1) {
  uint triangles;
  uint indices;
  uint2 padding;
};

static const float UV_LIMIT = 2.f;
static const float TRIANGLE_REACH = 1.1f;
static const uint COLLAPSED = 0xC200C200u;  // (-3, -3) as halves: past UV_LIMIT, so no kept corner is one
static const uint UNKNOWN_OPAQUE = 0xFFFFFFFCu;  // D3D12's special micromap index -4

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= triangles) return;
  const uint3 packed = source.Load3(id.x * 12u);
  if (indices != 0u) {
    if (all(packed == COLLAPSED)) target.Store(id.x * 4u, UNKNOWN_OPAQUE);
    return;
  }
  float2 low = UV_LIMIT;
  float2 high = -UV_LIMIT;
  bool kept = true;
  [unroll]
  for (uint corner = 0; corner < 3u; ++corner) {
    const float2 uv = float2(f16tof32(packed[corner]), f16tof32(packed[corner] >> 16));
    // A NaN fails the comparison too.
    kept = kept && all(abs(uv) <= UV_LIMIT);
    low = min(low, uv);
    high = max(high, uv);
  }
  target.Store3(id.x * 12u, (kept && all(high - low <= TRIANGLE_REACH)) ? packed : COLLAPSED);
}
