/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (screen_space_shadows.hpp): the scene's depth (a copy of the depth-stencil buffer, or the
// G-buffer's channel 4; its x; with the game's MSAA its sample 0, SSS_DEPTH_MS) into a single-channel texture Bend's
// sampler reads. With Light Through Foliage the foliage's pixels (t1, t2 the depth before and after the foliage's depth
// pass, none bound reads 0: it changed the pixel, nothing nearer came after; or where the G-buffer marks them, t3's alpha
// byte with g_sss_foliage_bit, Warhammer III's) negative: Bend's march lets the light through what they shadow
// (sss_bend).
#include "./sss.hlsli"

#ifdef SSS_DEPTH_MS
Texture2DMS<float4> t_scene_depth : register(t0);
Texture2DMS<float4> t_before_foliage : register(t1);
Texture2DMS<float4> t_after_foliage : register(t2);
#else
Texture2D<float4> t_scene_depth : register(t0);
Texture2D<float4> t_before_foliage : register(t1);
Texture2D<float4> t_after_foliage : register(t2);
#endif
Texture2D<float4> t_material : register(t3);
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 coords : SV_DispatchThreadID)
{
#ifdef SSS_DEPTH_MS
  const float depth = t_scene_depth.Load(int2(coords.xy), 0).x;
  const float before = t_before_foliage.Load(int2(coords.xy), 0).x;
  const float after = t_after_foliage.Load(int2(coords.xy), 0).x;
#else
  const float depth = t_scene_depth.Load(int3(coords.xy, 0)).x;
  const float before = t_before_foliage.Load(int3(coords.xy, 0)).x;
  const float after = t_after_foliage.Load(int3(coords.xy, 0)).x;
#endif
  const bool marked = g_sss_foliage_bit > 0.f && (((uint)round(t_material.Load(int3(coords.xy, 0)).w * 255.f)) & (uint)g_sss_foliage_bit) != 0u;
  t_rw_depth[coords.xy] = (g_sss_foliage_light > 0.f && ((after != before && after == depth) || marked)) ? -depth : depth;
}
