/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's result into the game's DLSS output (upscaling.hpp; the tone mapper reads its colour alone).
Texture2D<float4> t_output : register(t0);
RWTexture2D<float4> u_target : register(u0);
cbuffer upscale_settings : register(b0) {
  float g_square_root;  // upscale_inputs'
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  u_target.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  float3 color = t_output[id.xy].rgb;
  if (g_square_root != 0.f) color *= abs(color);
  u_target[id.xy] = float4(color, 1.f);
}
