/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (screen_space_shadows.hpp), a one-time report for ReShade.log: the camera's registers (b0), the
// sun's (b1), sss_setup's light coordinate (t2) and the depth at 8 points from Bend's input (t0) and from the G-buffer's
// depth channel (t1), so the depth's mapping can be compared with the camera's.
cbuffer camera_VS_PS : register(b0)
{
  float4 camera_registers[36];
}

cbuffer lighting_VS_PS : register(b1)
{
  float4 lighting_registers[2];
}

Texture2D<float> t_sss_depth : register(t0);
Texture2D<float> t_gbuffer_depth : register(t1);
StructuredBuffer<int4> t_sss_parameters : register(t2);
RWStructuredBuffer<float4> t_rw_probe : register(u0);

[numthreads(1, 1, 1)]
void main()
{
  [loop]
  for (uint i = 0; i < 36; ++i) t_rw_probe[i] = camera_registers[i];
  t_rw_probe[36] = lighting_registers[0];
  t_rw_probe[37] = asfloat(t_sss_parameters[0]);
  uint2 size;
  t_sss_depth.GetDimensions(size.x, size.y);
  static const float2 POINTS[8] = {float2(0.5f, 0.5f), float2(0.5f, 0.9f), float2(0.5f, 0.1f), float2(0.1f, 0.5f),
                                   float2(0.9f, 0.5f), float2(0.25f, 0.75f), float2(0.75f, 0.75f), float2(0.5f, 0.7f)};
  [unroll]
  for (uint p = 0; p < 8; ++p) {
    const uint2 pixel = uint2(POINTS[p] * size);
    t_rw_probe[38 + p] = float4(t_sss_depth.Load(int3(pixel, 0)), t_gbuffer_depth.Load(int3(pixel, 0)), pixel);
  }
}
