/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// CMAA2's working copy (u0): the tone mapper's output (t0, 2.0 encoded signed BT.709; linear with TW_AA_FRAME_LINEAR) as
// linear BT.2020, which CMAA2 expects (its colours stored without sign).
#include "./aa.hlsli"
#include "./shared.h"

// The tone mapper's output: 0 its square root (Attila, Thrones, WH3...), 1 linear (Pharaoh, Troy: their wrappers define it).
#ifndef TW_AA_FRAME_LINEAR
#define TW_AA_FRAME_LINEAR 0
#endif

Texture2D<float4> t_aa_color : register(t0);
RWTexture2D<float4> t_rw_work : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= g_aa_metrics.zw)) return;
  const float4 color = t_aa_color[pixel];
  const float3 bt709 = (TW_AA_FRAME_LINEAR ? color.rgb : renodx::color::gamma::DecodeSafe(color.rgb, 2.f));
  t_rw_work[pixel] = float4(max(renodx::color::bt2020::from::BT709(bt709), 0.f), color.a);
}
