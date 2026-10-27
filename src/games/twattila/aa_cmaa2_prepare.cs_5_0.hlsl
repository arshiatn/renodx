/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// CMAA2's working copy (u0): the tone mapper's output (t0, 2.0 encoded signed BT.709) as linear BT.2020, which CMAA2
// expects (its colours stored without sign).
#include "./aa.hlsli"
#include "./shared.h"

Texture2D<float4> t_aa_color : register(t0);
RWTexture2D<float4> t_rw_work : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= g_aa_metrics.zw)) return;
  const float4 color = t_aa_color[pixel];
  t_rw_work[pixel] = float4(max(renodx::color::bt2020::from::BT709(renodx::color::gamma::DecodeSafe(color.rgb, 2.f)), 0.f),
                            color.a);
}
