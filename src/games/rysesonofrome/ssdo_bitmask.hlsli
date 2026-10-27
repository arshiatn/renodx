/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_RYSESONOFROME_SSDO_BITMASK_HLSLI_
#define SRC_GAMES_RYSESONOFROME_SSDO_BITMASK_HLSLI_

#include "../twattila/bitmask.hlsli"

// Ambient occlusion GT-VBAO for Ryse's SSDO pass (ssdo00, SI.ssao_*): the visibility bitmask (twattila/bitmask.hlsli,
// as Attila's and Tales') on the pass's own linear depth, in its camera space: (uv x ViewSpaceParams.xy +
// ViewSpaceParams.zw, 1) x the depth in metres (the texture's 0-1 x the far plane), the G-buffer's normal through
// SSDO_CameraMatrix. It gives what the game's SSDO gives: the direction toward what covers the pixel (camera space, as
// long as the amount) and the amount, in the units the tiled lighting reads (its ambient term: 1 - 0.35 x amount x its
// SSDO amount, 1 by default, so 0.35 x amount = 1 - visibility). No temporal smoothing of its own: the noise moves every
// frame (SI.ssao_frame) and the game's SSDO blur and TAA / DLSS settle it.
float3 RyseAoPosition(float2 uv, float depth, float4 view_space_params) {
  return float3(uv * view_space_params.xy + view_space_params.zw, 1.f) * depth;
}

float2 RyseAoUv(float3 position, float4 view_space_params) {
  return (position.xy / position.z - view_space_params.zw) / view_space_params.xy;
}

float4 RyseBitmaskOcclusion(float2 uv, uint2 pixel, float3 normal, Texture2D<float4> depth_texture, SamplerState depth_sampler,
                            float far, float4 view_space_params) {
  uint2 depth_size;
  depth_texture.GetDimensions(depth_size.x, depth_size.y);
  // Positions from depth texel centres (a depth paired with another uv is off its surface: a flat floor would occlude
  // itself).
  const float2 texel = 1.f / float2(depth_size);
  uv = (floor(uv * depth_size) + 0.5f) * texel;
  const float depth = depth_texture.SampleLevel(depth_sampler, uv, 0).x * far;
  const float radius = SI.ssao_radius;
  // The radius in uv at the pixel's depth (camera space x / y move by uv x ViewSpaceParams.xy per metre of depth), and
  // in depth texels.
  const float2 radius_texels = radius / max(depth, 1e-3f) / abs(view_space_params.xy) * depth_size;
  const float radius_pixels = min(radius_texels.x, radius_texels.y);
  // The sky and pixels whose radius is under a texel: nothing to sample.
  if (!(depth < 0.999f * far) || radius_pixels < 1.f || !(dot(normal, normal) > 1e-6f)) return 0.f;

  normal = normalize(normal);
  const float3 position = RyseAoPosition(uv, depth, view_space_params);
  const float3 view = normalize(-position);
  if (dot(normal, view) < 0.f) normal = -normal;
  // A constant thickness behind each depth sample, growing with distance.
  const float thickness = SI.ssao_thickness * (1.f + 0.02f * depth);
  const float3 noises = BitmaskNoise(pixel, (uint)SI.ssao_frame);
  // Directions (slices): 2^Directions; samples per side of a slice: 8 x 2^Quality, at most one per texel of the radius.
  const uint slices = 1u << (uint)clamp(SI.ssao_directions, 0.f, 3.f);
  const uint steps = min(8u << (uint)clamp(SI.ssao_quality, 0.f, 3.f), max(4u, (uint)radius_pixels));
  float visibility = 0.f;
  float3 toward_occluders = 0.f;
  [loop]
  for (uint slice = 0; slice < slices; ++slice) {
    const BitmaskSlice slice_info = BitmaskSliceAt((slice + noises.x) / slices, normal, view, position);
    const float slice_jitter = frac(noises.z + 0.618034f * slice);
    // The slice's direction on screen: a short step along it projected, scaled to the radius.
    const float2 step_uv = (RyseAoUv(position + slice_info.direction * (0.01f * depth), view_space_params) - uv) * (radius / (0.01f * depth));
    uint occluded = 0u;
    // Until every sector is covered (nothing farther can change it).
    [loop]
    for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
      float distance = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
      // Denser near the pixel; at least one texel away (the centre itself is no occluder).
      distance = max(distance * distance, 1.f / radius_pixels);
      [unroll]
      for (int side = -1; side <= 1; side += 2) {
        const float2 sample_uv = (floor((uv + side * step_uv * distance) * depth_size) + 0.5f) * texel;
        // Off the screen: nothing known there (the edge's depth would make a false occluder).
        if (any(sample_uv != saturate(sample_uv))) continue;
        const float sample_depth = depth_texture.SampleLevel(depth_sampler, sample_uv, 0).x * far;
        const float3 sample_position = RyseAoPosition(sample_uv, sample_depth, view_space_params);
        const float3 delta = sample_position - position;
        const float delta_length = length(delta);
        if (!(delta_length > 1e-4f) || delta_length > radius) continue;
        // Its thickness along its own camera ray.
        const float3 back = normalize(delta + normalize(sample_position) * thickness);
        const uint covered = BitmaskSectors(delta / delta_length, back, view, side, slice_info, slice_jitter);
        // The direction toward the occluders, by the share each sample is the first to cover (as SSDO's weights).
        toward_occluders += (delta / delta_length) * countbits(covered & ~occluded);
        occluded |= covered;
      }
    }
    visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
  }
  const float amount = (1.f - exp2(log2(max(visibility / slices, 1e-4f)) * SI.ssao_strength)) / 0.35f;
  const float toward_length = length(toward_occluders);
  return float4((toward_length > 0.f) ? toward_occluders * (amount / toward_length) : 0.f, amount);
}

#endif  // SRC_GAMES_RYSESONOFROME_SSDO_BITMASK_HLSLI_
