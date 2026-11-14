/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's SSS Softness for Shogun 2's own screen-space shadows (screen_space_shadows.hpp, at the first sun light draw after
// them): with Softness or Debug View on, the game's mask held Bend's shadows alone and shaders/sss.hlsli kept the cascades
// apart (t2.r). Bend's shadows (t1, a copy of the mask) averaged over a fixed spiral disc of g_radius render pixels, only on
// the same surface (distance within 2%, from Bend's depth at t0: Attila's sss_soft, its foliage marked negative), then the
// darker of it and the cascades back into the mask (u0), as Bend's own min. Radius 0 (Debug View alone): that min only.
cbuffer soft : register(b0)
{
  float2 g_render_size;
  float g_radius;
}

Texture2D<float> t_sss_depth : register(t0);
Texture2D<float> t_bend : register(t1);
Texture2D<float4> t_split : register(t2);
RWTexture2D<unorm float> t_rw_shadow : register(u0);

static const int DISC_TAPS = 24;
static const float GOLDEN_ANGLE = 2.39996323f;

float SurfaceDistance(int2 pixel)
{
  return 1.f / max(1.f - abs(t_sss_depth[pixel]), 1e-7f);
}

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_render_size);
  if (any(int2(pixel) >= size)) return;
  float sum = t_bend[pixel];
  float total = 1.f;
  [branch] if (g_radius >= 1.f)
  {
    const float center_distance = SurfaceDistance(int2(pixel));
    const float same_surface = 0.02f * center_distance;
    [unroll]
    for (int disc = 0; disc < DISC_TAPS; ++disc)
    {
      const float share = sqrt(((float)disc + 0.5f) / (float)DISC_TAPS);
      const float angle = (float)disc * GOLDEN_ANGLE;
      const int2 tap = clamp(int2(pixel) + int2(round(float2(cos(angle), sin(angle)) * (share * g_radius))), 0, size - 1);
      // A tent over the disc.
      const float weight = (abs(SurfaceDistance(tap) - center_distance) <= same_surface) ? (1.f - 0.5f * share) : 0.f;
      sum += t_bend[tap] * weight;
      total += weight;
    }
  }
  t_rw_shadow[pixel] = min(t_split[pixel].r, sum / total);
}
