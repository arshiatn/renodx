/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SMAA's last pass: the colour (t0) blended with its neighbours by the weights (t1) into u0.
#include "./aa_smaa.hlsli"

Texture2D<float4> t_aa_color : register(t0);
Texture2D<float4> t_aa_weights : register(t1);
RWTexture2D<float4> t_rw_result : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  float2 texcoord;
  if (!AaTexcoord(pixel, texcoord)) return;
  float4 offset;
  SMAANeighborhoodBlendingVS(texcoord, offset);
  t_rw_result[pixel] = SMAANeighborhoodBlendingPS(texcoord, offset, t_aa_color, t_aa_weights);
}
