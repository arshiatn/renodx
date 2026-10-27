/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Total War ray tracing (rt_scene.hpp): a leaf material's opacity mask (its diffuse map's alpha, any format) into a
// slice of the atlas the helper tests ray hits against (dlaa_bridge.h RT_ATLAS_SIZE square, bilinear from the mip
// nearest that size, wrapping as the leaves sample).
Texture2D<float4> source : register(t0);
SamplerState linear_wrap : register(s0);
RWTexture2DArray<unorm float> atlas : register(u0);

cbuffer rt_atlas : register(b0)
{
  uint slice;
  float mip;
  uint size;
  uint padding;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (any(id.xy >= size)) return;
  const float2 uv = (float2(id.xy) + 0.5f) / (float)size;
  atlas[uint3(id.xy, slice)] = source.SampleLevel(linear_wrap, uv, mip).w;
}
