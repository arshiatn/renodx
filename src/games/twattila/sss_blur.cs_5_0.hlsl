/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows' Softness (screen_space_shadows.hpp): Bend's occlusion (t1) averaged over 3 x 3 taps
// g_sss_softness pixels apart, only those on the same surface (distance within 2%, from the depth at t0: standard
// depth, distance ~ 1 / (1 - depth)), into the mask the patched sun shaders read: smoother shadow edges, none across
// silhouettes.
#include "./sss.hlsli"

Texture2D<float> t_sss_depth : register(t0);
Texture2D<unorm float> t_sss_occlusion : register(t1);
RWTexture2D<unorm float> t_rw_soft : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_sss_render_size);
  if (any(int2(pixel) >= size)) return;
  const float center_distance = 1.f / max(1.f - abs(t_sss_depth[pixel]), 1e-7f);
  const int spacing = (int)g_sss_softness;
  float sum = 0.f;
  float total = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y)
  {
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
      const int2 tap = clamp(int2(pixel) + int2(x, y) * spacing, 0, size - 1);
      const float distance = 1.f / max(1.f - abs(t_sss_depth[tap]), 1e-7f);
      // A tent: the centre 1, the sides 1/2, the corners 1/4.
      const float tent = 1.f / float(1 << (abs(x) + abs(y)));
      const float weight = (abs(distance - center_distance) <= 0.02f * center_distance) ? tent : 0.f;
      sum += t_sss_occlusion[tap] * weight;
      total += weight;
    }
  }
  t_rw_soft[pixel] = sum / total;
}
