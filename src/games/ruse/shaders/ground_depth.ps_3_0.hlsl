/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced ground shadows (ray_tracing.hpp): the ground pre-pass's INTZ depth into the bridge's shared R32F depth, the
// positions the rays start from.
sampler2D depth : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
  return tex2Dlod(depth, float4(uv, 0.f, 0.f)).x;
}
