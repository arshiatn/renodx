/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smoke shadows without rays (smoke_map.hpp): a splat into the map, added (optical depths add). (1 - r^2)^3 is close to a
// Gaussian (exp(-3 r^2)) and ends at its edge, which is why spreading it adds radii squared.
float4 main(float4 position : SV_Position, float2 disk : TEXCOORD0, nointerpolation float4 optical : TEXCOORD1) : SV_Target
{
  const float falloff = saturate(1.f - dot(disk, disk));
  return optical * falloff * falloff * falloff;
}
