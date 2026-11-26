/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// CMAA2's working copy (u0): the tone mapper's output (t0, signed BT.709; Attila's t00 linear) squared (to 2.2 with aa.hlsli
// AaFrameGamma22; as it is with TW_AA_FRAME_LINEAR or AaFrameLinear) in BT.2020 as CMAA2's linear input (its colours
// stored without sign).
#include "./aa.hlsli"
#include "./shared.h"

// 0: read as 2.0 encoded (WH3...; Attila / Thrones in SDR, though their t00 is linear; in HDR aa.hlsli AaFrameLinear),
// 1 linear (Pharaoh, Troy define it).
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
  const float3 bt709 = ((TW_AA_FRAME_LINEAR || AaFrameLinear()) ? color.rgb : (AaFrameGamma22() ? AaDecodeFrame22(color.rgb) : renodx::color::gamma::DecodeSafe(color.rgb, 2.f)));
  t_rw_work[pixel] = float4(max(renodx::color::bt2020::from::BT709(bt709), 0.f), color.a);
}
