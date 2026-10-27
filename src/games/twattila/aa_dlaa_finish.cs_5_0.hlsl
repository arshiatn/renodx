/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// DLAA (anti_aliasing.hpp): NVIDIA's result (t0, linear BT.2020) back to the tone mapper's signed BT.709 and its 2.0
// encoding (square root, sign kept), the frame's own alpha (t1), into the result (u0).
#include "./aa.hlsli"

Texture2D<float4> t_dlaa : register(t0);
Texture2D<float4> t_current : register(t1);
RWTexture2D<float4> t_rw_result : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  if (any(int2(pixel) >= int2(g_aa_metrics.zw))) return;
  const float3 linear_color = mul(float3x3(1.6604910f, -0.5876411f, -0.0728499f,
                                           -0.1245505f, 1.1328999f, -0.0083494f,
                                           -0.0181508f, -0.1005789f, 1.1187297f),
                                  t_dlaa.Load(int3(pixel, 0)).rgb);
  t_rw_result[pixel] = float4(sign(linear_color) * sqrt(abs(linear_color)), t_current.Load(int3(pixel, 0)).a);
}
