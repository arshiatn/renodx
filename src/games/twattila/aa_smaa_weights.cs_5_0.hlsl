/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SMAA's second pass: the blending weights from the edges (t0), its area (t1) and search (t2) textures into u0.
#include "./aa_smaa.hlsli"

Texture2D<float4> t_aa_edges : register(t0);
Texture2D<float4> t_aa_area : register(t1);
Texture2D<float4> t_aa_search : register(t2);
RWTexture2D<unorm float4> t_rw_weights : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  float2 texcoord;
  if (!AaTexcoord(pixel, texcoord)) return;
  float2 pixcoord;
  float4 offset[3];
  SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
  t_rw_weights[pixel] = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, t_aa_edges, t_aa_area, t_aa_search,
                                                        g_smaa_subsample_indices);
}
