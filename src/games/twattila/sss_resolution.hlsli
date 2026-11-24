/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_SSS_RESOLUTION_HLSLI_
#define SRC_GAMES_TWATTILA_SSS_RESOLUTION_HLSLI_

// Screen-space shadows' Resolution below Full (screen_space_shadows.hpp): the low march's size (b3: the start of its
// sss_settings, the size it marches over, all of it rendered) and the render pixel each of its pixels stands for.
#include "./sss.hlsli"

cbuffer sss_low_settings : register(b3)
{
  float2 g_sss_low_size;
}

// The render pixel under low pixel `low`'s centre (sss_downsample reads it, sss_upsample weighs by it).
int2 LowRenderPixel(int2 low)
{
  return min(int2((float2(low) + 0.5f) * (g_sss_render_size / g_sss_low_size)), int2(g_sss_render_size) - 1);
}

#endif  // SRC_GAMES_TWATTILA_SSS_RESOLUTION_HLSLI_
