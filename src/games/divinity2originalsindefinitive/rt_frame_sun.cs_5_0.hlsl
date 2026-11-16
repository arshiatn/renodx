/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing (ray_tracing.hpp): the sun's direction for the helper's trace from the game's PerFrame (b13,
// GlobalConstants_DX11.shdh: global_LightPropertyMatrix, a column-major float4x4: its row 0, toward the sun, is c0-c2's x,
// as ClusteredLightingWithSSAO's bytes read it) into the bridge's sun texture's texel 0 (rt_core.hpp WriteSun's layout;
// texel 1 is rt_positions' camera).
cbuffer frame_constants : register(b0)
{
  float4 frame_registers[4];
}

RWTexture2D<float4> sun : register(u0);

[numthreads(1, 1, 1)]
void main()
{
  const float3 toward_sun = float3(frame_registers[0].x, frame_registers[1].x, frame_registers[2].x);
  sun[uint2(0, 0)] = float4(toward_sun * rsqrt(max(dot(toward_sun, toward_sun), 1e-12f)), 1.f);
}
