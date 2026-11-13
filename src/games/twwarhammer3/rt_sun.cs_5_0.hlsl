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
// With the game's Global Illumination on (c11.y) the sun shaders take neither inside its volumes but its light probes
// (9EAD8951: (visibility, ambient) = lerp((1, either), the probes', their fade)), and a probe's ray that meets nothing brings
// the environment maps (t_specular_array by g_environment_weights) x g_ambient_fudge_factor (cs DC860C60). So with rt_sun.w
// (Path-Traced AO, its row) and the flag on, each side is that: the environment maps' cosine-weighted mean over the side's
// half of the sky x the fudge factor, the light an open surface facing it gets from the game's probes; texel 4's w then 1.
// With sun_colour's place (b1's second x; 0: none) the sun's diffuse light into texel 2 (w 1), the paths' sun with Path-Traced
// Ambient (dlaa_helper/rt_cache_resolve.cs_6_5.hlsl): all 7 sun light shaders light their diffuse colour (albedo x (1 - metal)
// x (1 - Fresnel), the guide's) by sun_colour (c1) x N.L x the shadow x 0x389FB405 (7.615242e-05), the literal in all 7.
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[19];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;        // x the first float of sun_direction, y of the ambient cube, z 1 with Path-Traced AO on, w 1: the probes' sky
  uint4 rt_sun_light;  // x the first float of sun_colour (0: none)
}

cbuffer camera : register(b2)
{
  float3 camera_position;
}

// The sun draw's environment_PS: g_environment_weights[8] in c0-c7's x, g_environment_indices[8] in c8-c15's x, the count c15.y.
cbuffer environment_PS : register(b3)
{
  float4 environment[16];
}

Texture2D<float4> world_spherical_harmonics : register(t0);
TextureCubeArray<float4> specular_array : register(t1);
SamplerState specular_sampler : register(s0);
RWTexture2D<float4> sun : register(u0);

static const uint SKY_RAYS = 64u;
// The mip whose faces are about this wide: a ray's share of a half sky (2 pi / 64) is about a texel there.
static const float SKY_FACE_TEXELS = 8.f;

groupshared float3 side_sums[6][SKY_RAYS];

float LightingValue(uint index)
{
  return lighting[index / 4u][index % 4u];
}

[numthreads(SKY_RAYS, 1, 1)]
void main(uint3 thread : SV_GroupThreadID)
{
  const bool spherical_harmonics = asuint(lighting[11].z) != 0u;
  const uint maps = (uint)clamp(round(environment[15].y), 0.f, 8.f);
  const bool probe_sky = rt_sun.z != 0u && rt_sun.w != 0u && asuint(lighting[11].y) != 0u && maps != 0u;
  if (probe_sky)
  {
    uint width, height, elements, levels;
    specular_array.GetDimensions(0, width, height, elements, levels);
    const float mip = clamp(log2(max((float)width, 1.f) / SKY_FACE_TEXELS), 0.f, (float)max(levels, 1u) - 1.f);
    // Cosine-weighted around each side (stratified in the cosine, the angle by the golden ratio).
    const float u = (thread.x + 0.5f) / SKY_RAYS;
    const float angle = 6.2831853f * frac(thread.x * 0.618034f);
    const float3 local = float3(sqrt(u) * cos(angle), sqrt(u) * sin(angle), sqrt(1.f - u));
    [unroll]
    for (uint side = 0u; side < 6u; ++side)
    {
      float3 axis = 0.f;
      axis[side / 2u] = ((side & 1u) != 0u) ? -1.f : 1.f;
      const float3 helper = (abs(axis.y) < 0.5f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
      const float3 tangent = normalize(cross(helper, axis));
      const float3 direction = local.x * tangent + local.y * cross(axis, tangent) + local.z * axis;
      float3 sky = 0.f;
      for (uint map = 0u; map < maps; ++map)
      {
        sky += specular_array.SampleLevel(specular_sampler, float4(direction, environment[8u + map].x), mip).rgb * environment[map].x;
      }
      side_sums[side][thread.x] = sky;
    }
  }
  GroupMemoryBarrierWithGroupSync();
  if (thread.x != 0u) return;

  const float3 sun_direction = float3(LightingValue(rt_sun.x), LightingValue(rt_sun.x + 1u), LightingValue(rt_sun.x + 2u));
  const float length_squared = dot(sun_direction, sun_direction);
  sun[uint2(0, 0)] = (length_squared > 0.f) ? float4(-sun_direction * rsqrt(length_squared), 1.f) : 0.f;
  const uint colour = rt_sun_light.x;
  const float3 sun_colour = float3(LightingValue(colour), LightingValue(colour + 1u), LightingValue(colour + 2u));
  sun[uint2(2, 0)] = (colour != 0u) ? float4(sun_colour * asfloat(0x389FB405u), 1.f) : 0.f;

  float3 sides[6];
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    sides[side] = float3(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u));
  }
  float scale = lighting[18].w;
  if (probe_sky)
  {
    [unroll]
    for (uint side = 0u; side < 6u; ++side)
    {
      float3 sum = 0.f;
      for (uint ray = 0u; ray < SKY_RAYS; ++ray)
      {
        sum += side_sums[side][ray];
      }
      sides[side] = max(sum / SKY_RAYS, 0.f) * lighting[18].w;
    }
    scale = 1.f;
  }
  else if (spherical_harmonics)
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
