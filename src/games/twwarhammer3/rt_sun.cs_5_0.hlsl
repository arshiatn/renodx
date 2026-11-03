/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's ray tracing shader rt_sun (twattila/rt_scene.hpp): at the frame's first sun light draw, the direction
// toward the sun into the sun texture's texel 0 and the game's ambient cube (lighting_VS_PS c2-c7: +x, -x, +y, -y, +z, -z) into
// texels 3-8, the paths' sky (dlaa_helper/rt_path.hlsli SkyLight). Its sun light shaders take the cube at the normal, squared,
// x g_ambient_fudge_factor (c18.w), or without that factor with spherical harmonics on (c11.z): texel 4's w, the paths' sky
// in the game's units. Texel 6's w 0: no stretch of the normal in the sky's calibration. A cube of all zeros isn't this
// place's ambient: texel 3's w 0, none.
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[19];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;  // x the first float of sun_direction, y of the ambient cube
}

RWTexture2D<float4> sun : register(u0);

float LightingValue(uint index)
{
  return lighting[index / 4u][index % 4u];
}

[numthreads(1, 1, 1)]
void main()
{
  const float3 sun_direction = float3(LightingValue(rt_sun.x), LightingValue(rt_sun.x + 1u), LightingValue(rt_sun.x + 2u));
  const float length_squared = dot(sun_direction, sun_direction);
  sun[uint2(0, 0)] = (length_squared > 0.f) ? float4(-sun_direction * rsqrt(length_squared), 1.f) : 0.f;
  // c11.z and c18.w: nine registers and two floats, sixteen registers and three floats past the cube's c2.
  const bool spherical_harmonics = asuint(LightingValue(rt_sun.y + 38u)) != 0u;
  const float scale = spherical_harmonics ? 1.f : LightingValue(rt_sun.y + 67u);
  float3 any_side = 0.f;
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    any_side = max(any_side, abs(float3(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u))));
  }
  const bool has_cube = any(any_side > 0.f);
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    const float w = (side == 1u) ? scale : ((side == 3u) ? 0.f : 1.f);
    sun[uint2(3u + side, 0)] = has_cube ? float4(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u), w) : 0.f;
  }
}
