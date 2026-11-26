/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's SSS Softness and Softer Away from the Caster for Shogun 2's own screen-space shadows (screen_space_shadows.hpp, at
// the first sun light draw after them): with those or the Debug View on, the game's mask held Bend's shadows alone and
// shaders/sss.hlsli kept the cascades apart (t2.r) with each shadowed pixel's penumbra (t2.g, a share of the most: Softer
// Away's extended pass). As Attila's sss_soft: a pixel takes the largest penumbra that reaches it from the taps around it
// on the same surface, at least g_softness; Bend's shadows (t1, a copy of the mask) averaged over a fixed spiral disc of that
// radius, only on the same surface (distance within 2%, from Bend's depth at t0, its foliage marked negative); then the
// darker of it and the cascades back into the mask (u0), as Bend's own min. Radius under 1 (Debug View alone): that min only.
cbuffer soft : register(b0)
{
  float2 g_render_size;
  float g_softness;
  float g_most;  // the penumbras' most (render pixels: shared.h's SSS_MOST_PENUMBRA_2160 at this height); 0: none searched
}

// The pass's own sss constants (shaders/sss.hlsli's, copied at it): c1 Bend's light coordinate, its z the depth at
// infinity (X30, as Attila's sss_blur: distance ~ 1 / (A - depth); 1 - depth was too strict near the far plane).
cbuffer sss : register(b1)
{
  float4 g_sss_constants[2];
}

Texture2D<float> t_sss_depth : register(t0);
Texture2D<float> t_bend : register(t1);
Texture2D<float4> t_split : register(t2);
RWTexture2D<unorm float> t_rw_shadow : register(u0);

static const int SEARCH_TAPS = 16;  // two rings of 8: at the most radius and half of it
static const int DISC_TAPS = 24;
static const float GOLDEN_ANGLE = 2.39996323f;

float SurfaceDistance(int2 pixel)
{
  return 1.f / max(abs(g_sss_constants[1].z - abs(t_sss_depth[pixel])), 1e-7f);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_render_size);
  if (any(int2(pixel) >= size)) return;
  const float center_distance = SurfaceDistance(int2(pixel));
  const float same_surface = 0.02f * center_distance;
  float radius = g_softness;
  [branch] if (g_most > 0.f)
  {
    radius = max(radius, t_split[pixel].g * g_most);
    [unroll]
    for (int search = 0; search < SEARCH_TAPS; ++search)
    {
      const float angle = (float)search * (6.28318530f / 8.f) + ((search >= 8) ? 0.39269908f : 0.f);
      const float reach = g_most * ((search >= 8) ? 0.5f : 1.f);
      const int2 tap = clamp(int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * reach)), 0, size - 1);
      const float tap_radius = t_split[tap].g * g_most;
      if (tap_radius > reach && abs(SurfaceDistance(tap) - center_distance) <= same_surface) radius = max(radius, tap_radius);
    }
  }
  float sum = t_bend[pixel];
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
      sum += t_bend[tap] * weight;
      total += weight;
    }
  }
  t_rw_shadow[pixel] = min(t_split[pixel].r, sum / total);
}
