/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SMAA's first pass: luma edges of the colour (t0) into u0.
#include "./aa_smaa.hlsli"

Texture2D<float4> t_aa_color : register(t0);
RWTexture2D<unorm float2> t_rw_edges : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  float2 texcoord;
  if (!AaTexcoord(pixel, texcoord)) return;
  float4 offset[3];
  SMAAEdgeDetectionVS(texcoord, offset);
  t_rw_edges[pixel] = SMAALumaEdgeDetectionPS(texcoord, offset, t_aa_color);
}
