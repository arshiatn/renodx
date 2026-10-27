/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_scene.hpp): after the game's ambient occlusion pass, its target multiplied by the traced
// ambient visibility (blend: destination x source, red only). The pass's viewport covers the render size at its own
// resolution: a pixel there is the traced one at x (render size / the viewport's).
Texture2D<float> ambient : register(t0);
SamplerState linear_clamp : register(s0);

cbuffer rt_ambient : register(b0)
{
  float4 rt_ambient;  // xy the render size over the pass's viewport, zw 1 / the traced texture's size
}

float4 main(float4 position : SV_Position) : SV_Target
{
  return ambient.SampleLevel(linear_clamp, position.xy * rt_ambient.xy * rt_ambient.zw, 0).xxxx;
}
