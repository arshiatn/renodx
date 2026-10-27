/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_UE_BITMASK_AO_HLSLI_
#define SRC_GAMES_TALESOFARISE_UE_BITMASK_AO_HLSLI_

#include "../twattila/bitmask.hlsli"

// Ambient occlusion GT-VBAO for UE 4.21's ambient occlusion pass (Tales of Arise, Expeditions: Rome; SI.ssao_* from
// the includer's shared.h): the visibility bitmask (twattila/bitmask.hlsli, as Attila's) on the pass's own depth. View
// space as UE's AO: x right, y up, z forward, (screen x z / projection.x, screen y z / projection.y, z) from a screen
// position (-1..1, y up); the depth texture at `screen * depth_scale + depth_offset`, its value to linear depth (cm)
// through `device_z_to_depth` (View.InvDeviceZToWorldZTransform; a linear depth: (1, 0, 0, -1e30)). Radius and
// thickness in metres (UE's units: cm). No temporal smoothing of its own: the noise moves every frame (SI.ssao_frame)
// and the game's TAA / DLSS (and its AO blur) settle it, XeGTAO's way.
float UeAoDepth(float stored, float4 device_z_to_depth) {
  return stored * device_z_to_depth.x + device_z_to_depth.y + 1.f / (stored * device_z_to_depth.z - device_z_to_depth.w);
}

float3 UeAoViewPosition(float2 screen_position, float depth, float2 projection) {
  return float3(screen_position * depth / projection, depth);
}

// The visibility (1 open, 0 occluded, Strength as an exponent) of the pixel at `screen_position` with view-space
// `normal` (any length; none, as UE's unlit pixels: open) and its noise from `pixel`.
float UeBitmaskAmbientOcclusion(float2 screen_position, float3 normal, uint2 pixel, Texture2D<float4> depth_texture,
                                SamplerState depth_sampler, float2 depth_scale, float2 depth_offset, float2 projection,
                                float4 device_z_to_depth) {
  const float depth =
      UeAoDepth(depth_texture.SampleLevel(depth_sampler, screen_position * depth_scale + depth_offset, 0).x, device_z_to_depth);
  const float radius = 100.f * SI.ssao_radius;
  // The radius in screen units at the pixel's depth, and in depth texels.
  const float2 radius_screen = radius * projection / max(depth, 1e-3f);
  uint2 depth_size;
  depth_texture.GetDimensions(depth_size.x, depth_size.y);
  const float2 radius_texels = radius_screen * abs(depth_scale) * depth_size;
  const float radius_pixels = min(radius_texels.x, radius_texels.y);
  // The sky and pixels whose radius is under a texel: nothing to sample.
  if (!(depth < 60000.f) || radius_pixels < 1.f || !(dot(normal, normal) > 1e-6f)) return 1.f;

  normal = normalize(normal);
  const float3 position = UeAoViewPosition(screen_position, depth, projection);
  const float3 view = normalize(-position);
  if (dot(normal, view) < 0.f) normal = -normal;

  // A constant thickness behind each depth sample, growing with distance (the paper's option against occlusion fading
  // far away).
  const float thickness = 100.f * SI.ssao_thickness * (1.f + 0.0002f * depth);
  const float3 noises = BitmaskNoise(pixel, (uint)SI.ssao_frame);
  // Directions (slices) around the view vector: 2^Directions; samples per side of a slice: 8 x 2^Quality, at most one
  // per depth texel of the radius.
  const uint slices = 1u << (uint)clamp(SI.ssao_directions, 0.f, 3.f);
  const uint steps = min(8u << (uint)clamp(SI.ssao_quality, 0.f, 3.f), max(4u, (uint)radius_pixels));
  float visibility = 0.f;
  [loop]
  for (uint slice = 0; slice < slices; ++slice) {
    const BitmaskSlice slice_info = BitmaskSliceAt((slice + noises.x) / slices, normal, view, position);
    const float slice_jitter = frac(noises.z + 0.618034f * slice);
    // The slice's screen direction runs down with pixels; screen positions run up.
    const float2 step_screen = float2(slice_info.screen_direction.x, -slice_info.screen_direction.y) * radius_screen;
    uint occluded = 0u;
    // Until every sector is covered (nothing farther can change it).
    [loop]
    for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
      float distance = (step + frac(noises.y + 0.618034f * (step + slice * steps))) / steps;
      // Denser near the pixel; at least one texel away (the centre itself is no occluder).
      distance = max(distance * distance, 1.f / radius_pixels);
      const float2 offset = step_screen * distance;
      [unroll]
      for (int side = -1; side <= 1; side += 2) {
        const float2 sample_screen = screen_position + side * offset;
        const float sample_depth = UeAoDepth(
            depth_texture.SampleLevel(depth_sampler, sample_screen * depth_scale + depth_offset, 0).x, device_z_to_depth);
        const float3 sample_position = UeAoViewPosition(sample_screen, sample_depth, projection);
        const float3 delta = sample_position - position;
        const float delta_length = length(delta);
        if (!(delta_length > 0.01f) || delta_length > radius) continue;
        // Its thickness along its own camera ray.
        const float3 back = normalize(delta + normalize(sample_position) * thickness);
        occluded |= BitmaskSectors(delta / delta_length, back, view, side, slice_info, slice_jitter);
      }
    }
    visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
  }
  return exp2(log2(max(visibility / slices, 1e-4f)) * SI.ssao_strength);
}

#endif  // SRC_GAMES_TALESOFARISE_UE_BITMASK_AO_HLSLI_
