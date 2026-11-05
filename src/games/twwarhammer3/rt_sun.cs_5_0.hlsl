/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's ray tracing shader rt_sun (twattila/rt_scene.hpp): at the frame's first sun light draw, the direction
// toward the sun into the sun texture's texel 0 and the sky into texels 3-8 as six sides (+x, -x, +y, -y, +z, -z), the paths'
// sky (dlaa_helper/rt_path.hlsli SkyLight). Texel 4's w: the sky's scale in the game's units. Texel 6's w 0: no stretch of the
// normal in the sky's calibration. Texel 3's w 0: no sky, the paths take the learned shade.
// Spherical harmonics off (c11.z): the sun light shaders take the ambient cube (lighting_VS_PS c2-c7) at the normal, squared,
// x g_ambient_fudge_factor (c18.w). On: the cube is only what they fade to outside the playable area (9EAD8951: 0 to 0.07, sides
// at 0); the sky is t_world_spherical_harmonics, 9EAD8951's loop at the camera's place, evaluated along the six sides. Only
// Path-Traced AO's sky takes it: texel 3's w stays 0 without, so Light Bounce's escaping paths keep the learned shade.
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[19];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;  // x the first float of sun_direction, y of the ambient cube, z 1 with Path-Traced AO on
}

cbuffer camera : register(b2)
{
  float3 camera_position;
}

Texture2D<float4> world_spherical_harmonics : register(t0);
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

  float3 sides[6];
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    sides[side] = float3(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u));
  }
  const bool spherical_harmonics = asuint(lighting[11].z) != 0u;
  float scale = lighting[18].w;
  if (spherical_harmonics)
  {
    scale = 1.f;
    uint width, height;
    world_spherical_harmonics.GetDimensions(width, height);
    const int2 last_cell = int2(width / 3u, height / 3u) - 1;
    const float2 place = saturate((camera_position.xz - lighting[17].xy) / (lighting[17].zw - lighting[17].xy)) * float2(max(last_cell, 0));
    const int2 cell = int2(trunc(place));
    const int2 next_cell = min(cell + 1, last_cell);
    const float2 blend = frac(place);
    const int terms = (int)round(lighting[18].y);
    [unroll]
    for (uint side = 0u; side < 6u; ++side)
    {
      float3 normal = 0.f;
      normal[side / 2u] = ((side & 1u) != 0u) ? -1.f : 1.f;
      const float basis[9] = {1.f, normal.x, normal.y, normal.z, normal.x * normal.z, normal.z * normal.y, normal.y * normal.x,
                              normal.z * normal.z * 3.f - 1.f, normal.x * normal.x - normal.y * normal.y};
      float3 sky = 0.f;
      [unroll]
      for (int term = 0; term < 9; ++term)
      {
        if (term >= terms) break;
        const int2 slot = int2(term % 3, term / 3);
        const float3 near_row = lerp(world_spherical_harmonics.Load(int3(cell * 3 + slot, 0)).rgb,
                                     world_spherical_harmonics.Load(int3(int2(next_cell.x, cell.y) * 3 + slot, 0)).rgb, blend.x);
        const float3 far_row = lerp(world_spherical_harmonics.Load(int3(int2(cell.x, next_cell.y) * 3 + slot, 0)).rgb,
                                    world_spherical_harmonics.Load(int3(next_cell * 3 + slot, 0)).rgb, blend.x);
        sky += lerp(near_row, far_row, blend.y) * basis[term];
      }
      sides[side] = max(sky, 0.f);
    }
  }
  float3 any_side = 0.f;
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    any_side = max(any_side, abs(sides[side]));
  }
  const bool has_sky = any(any_side > 0.f);
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const float no_sky = (spherical_harmonics && rt_sun.z == 0u) ? 0.f : 1.f;
    const float w = (side == 0u) ? no_sky : ((side == 1u) ? scale : ((side == 3u) ? 0.f : 1.f));
    sun[uint2(3u + side, 0)] = has_sky ? float4(sides[side], w) : 0.f;
  }
}
