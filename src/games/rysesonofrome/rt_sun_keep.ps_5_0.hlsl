/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rays Replace Shadow Maps (contact_shadows.hpp): multiplied into slice 0 of the sun's shadow mask right before
// 0x52C5E003 max-blends the rays' shadow in. 0 in the sun's channels (0x52C5E001's), 1 in the others and where the
// game's sky is (x 1 keeps them bit for bit): after both, the sun's channels hold the rays' shadow alone.
Texture2D<float> depth_texture : register(t0);  // linear depth / far
Buffer<uint> sun : register(t1);                // shadow mask slice, channels (0x52C5E001)

float4 main(float4 position : SV_Position) : SV_Target0 {
  // 0x52C5E003's own tests: only slice 0's target is bound, and the sky gets nothing from it.
  const uint channels = (sun[0] == 0u && depth_texture.Load(int3(uint2(position.xy), 0)) < 0.999f) ? sun[1] : 0u;
  return float4((channels & 1u) ? 0.f : 1.f, (channels & 2u) ? 0.f : 1.f, (channels & 4u) ? 0.f : 1.f, (channels & 8u) ? 0.f : 1.f);
}
