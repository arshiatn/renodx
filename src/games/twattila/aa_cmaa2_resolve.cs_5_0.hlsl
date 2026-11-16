/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// CMAA2's result (u0): the pixels it blended (the working copy, t0, no longer what aa_cmaa2_prepare wrote) back to the
// tone mapper's encoding (2.0; linear with TW_AA_FRAME_LINEAR); every other pixel the original (t1), untouched.
#include "./aa.hlsli"
#include "./shared.h"

// The tone mapper's output: 0 its square root (Attila, Thrones, WH3...), 1 linear (Pharaoh, Troy: their wrappers define it).
#ifndef TW_AA_FRAME_LINEAR
#define TW_AA_FRAME_LINEAR 0
#endif

Texture2D<float4> t_aa_work : register(t0);
Texture2D<float4> t_aa_color : register(t1);
RWTexture2D<float4> t_rw_result : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(float2(pixel) >= g_aa_metrics.zw)) return;
  const float4 color = t_aa_color[pixel];
  const float3 work = t_aa_work[pixel].rgb;
  // As the prepare stored it (16-bit).
  const float3 bt709 = (TW_AA_FRAME_LINEAR ? color.rgb : renodx::color::gamma::DecodeSafe(color.rgb, 2.f));
  const float3 prepared = f16tof32(f32tof16(max(renodx::color::bt2020::from::BT709(bt709), 0.f)));
  [branch]
  if (all(work == prepared))
  {
    t_rw_result[pixel] = color;
  }
  else
  {
    const float3 blended = renodx::color::bt709::from::BT2020(work);
    t_rw_result[pixel] = float4((TW_AA_FRAME_LINEAR ? blended : renodx::color::gamma::EncodeSafe(blended, 2.f)), color.a);
  }
}
