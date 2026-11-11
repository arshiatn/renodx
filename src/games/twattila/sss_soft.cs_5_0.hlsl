/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows' Softer Away from the Caster (screen_space_shadows.hpp): Bend's occlusion (t1) softened by the
// sun's disk as the extended march measured it (t2: each shadowed pixel's penumbra radius in render pixels, from its
// caster's distance). A pixel takes the largest radius whose penumbra reaches it from the taps around it on the same
// surface (it may lie in the penumbra of a shadow next to it), at least Softness, then the occlusion averaged over a
// fixed spiral disc of that radius, only on the same surface (distance within 2%, from the depth at t0, as sss_blur): sharp
// where things touch the ground, softer farther from what casts the shadow. The pattern stays the same every frame.
#include "./sss.hlsli"

Texture2D<float> t_sss_depth : register(t0);
Texture2D<unorm float> t_sss_occlusion : register(t1);
Texture2D<float> t_sss_penumbra : register(t2);
RWTexture2D<unorm float> t_rw_soft : register(u0);

static const int SEARCH_TAPS = 16;  // two rings of 8: at the most radius and half of it
static const int DISC_TAPS = 24;
static const float GOLDEN_ANGLE = 2.39996323f;

float SurfaceDistance(int2 pixel)
{
  return 1.f / max(1.f - abs(t_sss_depth[pixel]), 1e-7f);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_sss_render_size);
  if (any(int2(pixel) >= size)) return;
  const float center_distance = SurfaceDistance(int2(pixel));
  const float same_surface = 0.02f * center_distance;
  float radius = t_sss_penumbra[pixel];
  [unroll]
  for (int search = 0; search < SEARCH_TAPS; ++search)
  {
    const float angle = (float)search * (6.28318530f / 8.f) + ((search >= 8) ? 0.39269908f : 0.f);
    const float reach = g_sss_soft_radius * ((search >= 8) ? 0.5f : 1.f);
    const int2 tap = clamp(int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * reach)), 0, size - 1);
    const float tap_radius = t_sss_penumbra[tap];
    if (tap_radius > reach && abs(SurfaceDistance(tap) - center_distance) <= same_surface) radius = max(radius, tap_radius);
  }
  radius = max(radius, g_sss_softness);
  float sum = t_sss_occlusion[pixel];
  float total = 1.f;
  [branch] if (radius >= 1.f)
  {
    [unroll]
    for (int disc = 0; disc < DISC_TAPS; ++disc)
    {
      const float share = sqrt(((float)disc + 0.5f) / (float)DISC_TAPS);
      const float angle = (float)disc * GOLDEN_ANGLE;
      const int2 tap = clamp(int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * (share * radius))), 0, size - 1);
      // A tent over the disc.
      const float weight = (abs(SurfaceDistance(tap) - center_distance) <= same_surface) ? (1.f - 0.5f * share) : 0.f;
      sum += t_sss_occlusion[tap] * weight;
      total += weight;
    }
  }
  t_rw_soft[pixel] = sum / total;
}
