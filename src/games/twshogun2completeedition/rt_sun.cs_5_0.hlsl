/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's ray tracing shader rt_sun (twattila/rt_core.hpp; ray_tracing.hpp WriteSun), its own as Warhammer III's: at
// the frame's first sun light draw the way to the sun (texel 0), the sun's diffuse light in the game's units (texel 2:
// sunlight00-03 light the diffuse colour by sun_colour c1 x N.L x the shadow x 1.90384671e-005) and the sky (texels 3-8:
// +x, -x, +y, -y, +z, -z; dlaa_helper/rt_path.hlsli AmbientCube) as the sun light shaders' diffuse ambient at those
// normals. With spherical harmonics (lighting_VS_PS c10.w): g_cube_ambient_influence (c2.x) x the ambient cube (c3-c8,
// at the normal squared) + the rest x g_ambient_fudge_factor (c14.y) x the world's harmonics (t0; the previous t1 by
// g_specular_probe_lerp) at the camera's place in the probe grid (environment_PS c3-c5), evaluated at the six sides with
// the shaders' nine-term basis. Without: the fudge factor x the cube. Texel 3's w 1 (a sky), texel 4's w 1 (in the
// game's units), texel 6's w 0 (no stretch of the normal: the shaders take the cube at the normal as it is).
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[18];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;        // x the first float of sun_direction, y of the ambient cube (0: none)
  uint4 rt_sun_light;  // x the first float of sun_colour (0: none)
}

cbuffer camera_VS_PS : register(b2)
{
  float3 camera_position;
}

// The sun draw's environment_PS: g_probe_grid_area c3, g_probe_grid_size c4.x (uint), g_specular_probe_lerp c4.z,
// g_spherical_harmonic_terms c5.w.
cbuffer environment_PS : register(b3)
{
  float4 environment[6];
}

Texture2D<float4> world_spherical_harmonics : register(t0);
Texture2D<float4> prev_world_spherical_harmonics : register(t1);
RWTexture2D<float4> sun : register(u0);

static const float SUN_DIFFUSE_SCALE = 1.90384671e-005f;

float LightingValue(uint index)
{
  return lighting[index / 4u][index % 4u];
}

// The harmonics at the normal, bilinear between the four probes around `place` (the shaders' loop).
float3 Harmonics(Texture2D<float4> harmonics, int2 cell, int2 next_cell, float2 blend, float3 normal, int terms)
{
  const float basis[9] = {1.f, normal.x, normal.y, normal.z, normal.x * normal.z, normal.z * normal.y, normal.x * normal.y,
                          normal.z * normal.z * 3.f - 1.f, normal.x * normal.x - normal.y * normal.y};
  float3 sum = 0.f;
  [unroll]
  for (int term = 0; term < 9; ++term)
  {
    if (term >= terms) break;
    const int2 slot = int2(term % 3, term / 3);
    const float3 near_row = lerp(harmonics.Load(int3(cell * 3 + slot, 0)).rgb, harmonics.Load(int3(int2(next_cell.x, cell.y) * 3 + slot, 0)).rgb, blend.x);
    const float3 far_row = lerp(harmonics.Load(int3(int2(cell.x, next_cell.y) * 3 + slot, 0)).rgb, harmonics.Load(int3(next_cell * 3 + slot, 0)).rgb, blend.x);
    sum += lerp(near_row, far_row, blend.y) * basis[term];
  }
  return max(sum, 0.f);
}

[numthreads(1, 1, 1)]
void main()
{
  const float3 sun_direction = float3(LightingValue(rt_sun.x), LightingValue(rt_sun.x + 1u), LightingValue(rt_sun.x + 2u));
  const float length_squared = dot(sun_direction, sun_direction);
  sun[uint2(0, 0)] = (length_squared > 0.f) ? float4(-sun_direction * rsqrt(length_squared), 1.f) : 0.f;
  const uint colour = rt_sun_light.x;
  const float3 sun_colour = float3(LightingValue(colour), LightingValue(colour + 1u), LightingValue(colour + 2u));
  sun[uint2(2, 0)] = (colour != 0u) ? float4(sun_colour * SUN_DIFFUSE_SCALE, 1.f) : 0.f;
  if (rt_sun.y == 0u)
  {
    [unroll]
    for (uint side = 0u; side < 6u; ++side) sun[uint2(3u + side, 0)] = 0.f;
    return;
  }

  const bool spherical_harmonics = asuint(lighting[10].w) != 0u;
  const float influence = lighting[2].x;
  const float fudge = lighting[14].y;
  const int size = (int)asuint(environment[4].x);
  const float2 area_size = environment[3].zw - environment[3].xy;
  const float2 place = clamp((camera_position.xz - environment[3].xy) / area_size * size - 0.5f, 0.f, max(size - 1, 0));
  const int2 cell = int2(floor(place));
  const int2 next_cell = min(cell + 1, size - 1);
  const float2 blend = frac(place);
  const int terms = (int)round(environment[5].w);
  const float probe_lerp = environment[4].z;
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    const float3 cube = float3(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u));
    float3 sky = fudge * cube;
    if (spherical_harmonics && size > 0)
    {
      float3 normal = 0.f;
      normal[side / 2u] = ((side & 1u) != 0u) ? -1.f : 1.f;
      float3 harmonics = Harmonics(world_spherical_harmonics, cell, next_cell, blend, normal, terms);
      if (probe_lerp < 0.999f)
      {
        const float3 previous = Harmonics(prev_world_spherical_harmonics, cell, next_cell, blend, normal, terms);
        harmonics = previous + probe_lerp * (harmonics - previous);
      }
      sky = influence * cube + (1.f - influence) * fudge * harmonics;
    }
    sun[uint2(3u + side, 0)] = float4(sky, (side == 3u) ? 0.f : 1.f);
  }
}
