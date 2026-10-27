/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// TAA / SMAA T2x (anti_aliasing.hpp): the resolved image (t0) sharpened against its 4 neighbours (g_taa_sharpening x
// the difference to their mean), never beyond their range (no halos), into the result (u0). The history keeps the
// unsharpened image.
#include "./aa.hlsli"

Texture2D<float4> t_resolved : register(t0);
RWTexture2D<float4> t_rw_result : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  const int2 size = int2(g_aa_metrics.zw);
  if (any(int2(pixel) >= size)) return;
  const float4 center = t_resolved.Load(int3(pixel, 0));
  const float3 up = t_resolved.Load(int3(clamp(int2(pixel) + int2(0, -1), 0, size - 1), 0)).rgb;
  const float3 down = t_resolved.Load(int3(clamp(int2(pixel) + int2(0, 1), 0, size - 1), 0)).rgb;
  const float3 left = t_resolved.Load(int3(clamp(int2(pixel) + int2(-1, 0), 0, size - 1), 0)).rgb;
  const float3 right = t_resolved.Load(int3(clamp(int2(pixel) + int2(1, 0), 0, size - 1), 0)).rgb;
  const float3 low = min(center.rgb, min(min(up, down), min(left, right)));
  const float3 high = max(center.rgb, max(max(up, down), max(left, right)));
  const float3 sharpened = center.rgb + g_taa_sharpening * (center.rgb - 0.25f * (up + down + left + right));
  t_rw_result[pixel] = float4(clamp(sharpened, low, high), center.a);
}
