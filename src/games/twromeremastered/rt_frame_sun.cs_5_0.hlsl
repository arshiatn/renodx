/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing (ray_tracing.hpp): the sun's direction for the helper's trace from the forward pass's frame constants (the
// game's PS b8, bound at the AO apply and the shadow mask's resolve: c0.zw the inverse screen size, c1.xyz the camera's
// position, c2.xyz toward the sun (c2.y > 0 lights the sun's terms), c3.xyz its colour) into the bridge's sun texture's
// texel 0 (rt_core.hpp WriteSun's layout; texel 1 is rt_positions' camera).
cbuffer frame_constants : register(b0)
{
  float4 frame_registers[4];
}

RWTexture2D<float4> sun : register(u0);

[numthreads(1, 1, 1)]
void main()
{
  const float3 toward_sun = frame_registers[2].xyz;
  sun[uint2(0, 0)] = float4(toward_sun * rsqrt(max(dot(toward_sun, toward_sun), 1e-12f)), 1.f);
}
