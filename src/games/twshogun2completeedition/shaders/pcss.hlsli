/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Percentage-closer soft shadows (R. Fernando, "Percentage-Closer Soft Shadows", NVIDIA, 2005; written from the paper)
// for an orthographic shadow map (a sun cascade): contact-hardening shadows, sharp where the caster touches the
// receiver and softer the farther it is. Three steps: the blockers' average depth around the receiver within the widest
// penumbra (texels the comparison sampler finds in shadow: no assumption about the map's depth direction), the
// penumbra the sun's size makes across the gap between them and the receiver, then a comparison filter that wide.
// Samples on a Vogel disk, turned by `rotation` (per pixel and frame: the noise is settled by TAA / DLSS).
#ifndef SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_PCSS_HLSLI_
#define SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_PCSS_HLSLI_

static const uint PCSS_BLOCKER_SAMPLES = 16u;
static const uint PCSS_FILTER_SAMPLES = 24u;

// The `index`th of `count` points evenly over the unit disk (golden angle), turned by `rotation`.
float2 PcssVogelDisk(uint index, uint count, float rotation)
{
  const float radius = sqrt((index + 0.5f) / count);
  const float angle = index * 2.39996323f + rotation;
  return radius * float2(cos(angle), sin(angle));
}

// The penumbra's width in texels: the sun's size across the gap between the receiver (`receiver_depth`) and its
// blockers' average depth.
float PcssPenumbraTexels(float receiver_depth, float blocker_depth, float texel_world, float depth_world,
                         float sun_tangent, float max_texels)
{
  const float gap_world = abs(receiver_depth - blocker_depth) * depth_world;
  return clamp(gap_world * sun_tangent / texel_world, 1.f, max_texels);
}

// The lit share at `tc` (map uv, depth) in `slice`: `map_size` texels, `texel_world` / `depth_world` world units per
// texel and per unit of the map's depth, `sun_tangent` the tangent of the sun's angular radius, the penumbra at most
// `max_texels` wide (also the blocker search's reach).
float PcssShadow(Texture2DArray shadow_map, SamplerComparisonState comparison, float3 tc, uint slice, float2 map_size,
                 float texel_world, float depth_world, float sun_tangent, float max_texels, float rotation)
{
  float blocker_depth = 0.f;
  float blockers = 0.f;
  [loop]
  for (uint index = 0u; index < PCSS_BLOCKER_SAMPLES; ++index)
  {
    const float2 texel = floor(tc.xy * map_size + PcssVogelDisk(index, PCSS_BLOCKER_SAMPLES, rotation) * max_texels);
    const int2 coordinate = clamp(int2(texel), 0, int2(map_size) - 1);
    const float2 centre = (coordinate + 0.5f) / map_size;
    // In shadow there: a blocker, its depth.
    if (shadow_map.SampleCmpLevelZero(comparison, float3(centre, slice), tc.z) < 0.5f)
    {
      blocker_depth += shadow_map.Load(int4(coordinate, slice, 0)).x;
      blockers += 1.f;
    }
  }
  if (blockers == 0.f) return 1.f;
  const float penumbra_texels =
      PcssPenumbraTexels(tc.z, blocker_depth / blockers, texel_world, depth_world, sun_tangent, max_texels);
  float lit = 0.f;
  [loop]
  for (uint sample_index = 0u; sample_index < PCSS_FILTER_SAMPLES; ++sample_index)
  {
    const float2 offset = PcssVogelDisk(sample_index, PCSS_FILTER_SAMPLES, rotation) * penumbra_texels / map_size;
    lit += shadow_map.SampleCmpLevelZero(comparison, float3(tc.xy + offset, slice), tc.z);
  }
  return lit / PCSS_FILTER_SAMPLES;
}

// The same for a map of its own (Texture2D: Ryse's cascades).
float PcssShadow(Texture2D shadow_map, SamplerComparisonState comparison, float3 tc, float2 map_size,
                 float texel_world, float depth_world, float sun_tangent, float max_texels, float rotation)
{
  float blocker_depth = 0.f;
  float blockers = 0.f;
  [loop]
  for (uint index = 0u; index < PCSS_BLOCKER_SAMPLES; ++index)
  {
    const float2 texel = floor(tc.xy * map_size + PcssVogelDisk(index, PCSS_BLOCKER_SAMPLES, rotation) * max_texels);
    const int2 coordinate = clamp(int2(texel), 0, int2(map_size) - 1);
    const float2 centre = (coordinate + 0.5f) / map_size;
    if (shadow_map.SampleCmpLevelZero(comparison, centre, tc.z) < 0.5f)
    {
      blocker_depth += shadow_map.Load(int3(coordinate, 0)).x;
      blockers += 1.f;
    }
  }
  if (blockers == 0.f) return 1.f;
  const float penumbra_texels =
      PcssPenumbraTexels(tc.z, blocker_depth / blockers, texel_world, depth_world, sun_tangent, max_texels);
  float lit = 0.f;
  [loop]
  for (uint sample_index = 0u; sample_index < PCSS_FILTER_SAMPLES; ++sample_index)
  {
    const float2 offset = PcssVogelDisk(sample_index, PCSS_FILTER_SAMPLES, rotation) * penumbra_texels / map_size;
    lit += shadow_map.SampleCmpLevelZero(comparison, tc.xy + offset, tc.z);
  }
  return lit / PCSS_FILTER_SAMPLES;
}

// A per-pixel, per-frame turn for the disks (interleaved gradient noise, J. Jimenez 2014, moved by the frame).
float PcssRotation(float2 pixel, float frame)
{
  return 6.2831853f * frac(52.9829189f * frac(dot(pixel + 5.588238f * frame, float2(0.06711056f, 0.00583715f))));
}

#endif  // SRC_GAMES_TWSHOGUN2COMPLETEEDITION_SHADERS_PCSS_HLSLI_
