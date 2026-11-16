/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced AO in place of the game's SSAO (ray_tracing.hpp, at the sun pass 0xCCCB7253): the traced ambient visibility
// (t0, R8, 1 open, the render pixels at its top-left) drawn over the SSAO chain's result (the pass's Base, t5: half size,
// RGBA16F, the sun pass reads .x, the forward materials too), every channel, each texel the average of the render pixels it
// covers (a linear sample between them). The sun pass then multiplies its ambient by lerp(1, ao, z) and its sun light by
// lerp(1, ao, y) as before, with the rays' AO alone.
Texture2D<float> t_ambient : register(t0);
SamplerState s_linear : register(s0);

cbuffer rt_ambient : register(b0) {
  float4 g_texel;  // the target pixel's step in t0's UV (render / target size / t0's size)
};

float4 main(float4 position : SV_Position) : SV_Target {
  return t_ambient.SampleLevel(s_linear, position.xy * g_texel.xy, 0.f);
}
