/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows' Resolution below Full (screen_space_shadows.hpp): the march's inputs at its low size, each low
// pixel its render pixel's (sss_resolution.hlsli: the one under its centre, not blended, so the depth's edges stay
// edges): the depth (t0: sss_depth's, foliage's marked negative), the banners' depth (t1: Light Through Banners, b4's
// share above 0) and the G-buffer's colour (t2: Coloured Shadows) into u0-u2.
#include "./sss_resolution.hlsli"

Texture2D<float> t_sss_depth : register(t0);
Texture2D<float> t_sss_thin : register(t1);
Texture2D<float4> t_sss_colour : register(t2);
RWTexture2D<float> t_rw_depth : register(u0);
RWTexture2D<float> t_rw_thin : register(u1);
RWTexture2D<unorm float4> t_rw_colour : register(u2);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
  const int2 low = int2(id);
  if (any(low >= int2(g_sss_low_size))) return;
  const int2 rendered = LowRenderPixel(low);
  t_rw_depth[low] = t_sss_depth[rendered];
  [branch] if (g_sss_thin_light > 0.f) t_rw_thin[low] = t_sss_thin[rendered];
  [branch] if (g_sss_coloured != 0.f) t_rw_colour[low] = t_sss_colour[rendered];
}
