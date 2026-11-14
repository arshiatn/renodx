/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's ray tracing shader rt_sun (twattila/rt_scene.hpp; Warhammer II's form with WH1's lighting_VS_PS): at the
// frame's first sun light draw, the direction toward the sun into the sun texture's texel 0, the sun's diffuse light into
// texel 2 and the sky into texels 3-8 as six sides (+x, -x, +y, -y, +z, -z), the paths' sky (dlaa_helper/rt_path.hlsli
// SkyLight): what the 31 sun light shaders give a surface facing each side at the camera's place as their diffuse ambient
// (230F2241 asm 378-611). Spherical harmonics on (c11.z; every battle and campaign frame of ATN's 2026-11-14 traces):
// t_world_spherical_harmonics, the playable area (c17) in cells of 3 x 3 texels (term t at (t % 3, t / 3)), the place's
// fraction of it (clamped, then wrapped as the shaders' frac) x the cells, blended with the next cell (wrapping), all 9
// terms; texel 6's w 0 (the normal as it is). Off: the ambient cube (c2-c7), texel 6's w 1 (its y four times over,
// normalized, as the shaders take it). Texel 4's w 1 (the game's units). Texel 3's w 0 with the harmonics and Path-Traced
// AO off (rt_sun.z): Light Bounce's escaping paths keep the learned shade, as Warhammer II's and III's.
// With sun_colour's place (b1's second x; 0: none) the sun's diffuse light into texel 2 (w 1), the paths' sun with
// Path-Traced Ambient: the sun shaders light their diffuse colour by sun_colour (c1) x N.L x the shadow x 0.318310 x
// (g_hdr_on c11.x > 0 ? 0.01 : 1), all 31 the same; g_hdr_on 1 in ATN's traces. test/tw_warhammer_shaders checks both
// against 230F2241's own bytes on WARP.
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[18];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;        // x the first float of sun_direction, y of the ambient cube, z 1 with Path-Traced AO on
  uint4 rt_sun_light;  // x the first float of sun_colour (0: none)
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
  const uint colour = rt_sun_light.x;
  const float3 sun_colour = float3(LightingValue(colour), LightingValue(colour + 1u), LightingValue(colour + 2u));
  sun[uint2(2, 0)] = (colour != 0u) ? float4(sun_colour * 0.0031831f, 1.f) : 0.f;

  float3 sides[6];
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    sides[side] = float3(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u));
  }
  const bool spherical_harmonics = asuint(lighting[11].z) != 0u;
  if (spherical_harmonics)
  {
    uint width, height;
    world_spherical_harmonics.GetDimensions(width, height);
    const float2 cells = float2(uint2(width, height) / 3u);
    const float2 fraction = frac(saturate((camera_position.xz - lighting[17].xy) / (lighting[17].zw - lighting[17].xy)));
    const float2 place = fraction * cells;
    const int2 cell = int2(trunc(place));
    const int2 next_cell = int2(trunc(frac(fraction + 1.f / cells) * cells));
    const float2 blend = frac(place);
    [unroll]
    for (uint side = 0u; side < 6u; ++side)
    {
      float3 normal = 0.f;
      normal[side / 2u] = ((side & 1u) != 0u) ? -1.f : 1.f;
      const float basis[9] = {1.f, normal.x, normal.y, normal.z, normal.z * normal.x, normal.y * normal.z, normal.y * normal.x,
                              normal.z * normal.z * 3.f - 1.f, normal.x * normal.x - normal.y * normal.y};
      float3 sky = 0.f;
      [unroll]
      for (int term = 0; term < 9; ++term)
      {
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
    const float w = (side == 0u) ? no_sky : ((side == 3u) ? (spherical_harmonics ? 0.f : 1.f) : 1.f);
    sun[uint2(3u + side, 0)] = has_sky ? float4(sides[side], w) : 0.f;
  }
}
