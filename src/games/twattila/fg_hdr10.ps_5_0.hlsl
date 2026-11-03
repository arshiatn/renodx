/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Frame generation's frames from an scRGB swap chain (frame_generation.hpp): Streamline takes no scRGB. BT.709 with 1 = 80
// nits to BT.2020 PQ, the nits unchanged: what the swap chain shader's HDR10 end gives, to 10-bit rounding.
#include "./shared.h"

Texture2D t0 : register(t0);

float4 main(float4 vpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  const float3 bt2020 = renodx::color::bt2020::from::BT709(t0.Load(int3(vpos.xy, 0)).rgb);
  return float4(renodx::color::pq::EncodeSafe(bt2020, 80.f), 1.f);
}
