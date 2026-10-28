/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The depth upscale's triangle over the whole target (anti_aliasing.hpp UpscaleDepth, depth_upscale.ps).
float4 main(uint vertex : SV_VertexID) : SV_Position
{
  const float2 uv = float2((vertex << 1u) & 2u, vertex & 2u);
  return float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
}
