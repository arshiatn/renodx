/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_TWWARHAMMER2_BITMASK_AO_HLSLI_
#define SRC_TWWARHAMMER2_BITMASK_AO_HLSLI_

// Visibility bitmask ambient occlusion for the Warscape games' AO passes (as Attila's): GT-VBAO (twattila/bitmask.hlsli:
// Therrien, Levesque and Gilet's visibility bitmask with Mirko Salm's ground-truth weighting): each slice's occlusion
// kept as 32 sectors and every depth sample taken as a surface of constant thickness, so light passes behind thin
// objects (soldiers, spears, poles, trunks) instead of the dark halos a height field gives them. A slice's direction on
// screen comes from where its plane crosses the pixel's depth, so the view space's handedness does not matter. Depth Levels
// (twattila/depth_levels.hlsli, when the addon binds them for the pass): samples far from the pixel read the depth at a
// lower resolution, so they stay in the texture cache.
// The includer defines (after its shared.h, with SI.ssao_thickness, SI.ssao_quality, SI.ssao_directions and
// SI.ssao_auto_thickness):
//   float AoDepth(float2 uv)                       the depth there (any kind AoViewPosition takes)
//   float3 AoViewPosition(float2 uv, float depth)  the view-space position
#ifndef AO_NOISE_FRAME
#define AO_NOISE_FRAME 0.f
#endif

#include "../twattila/ao_upsample.hlsli"
#include "../twattila/bitmask.hlsli"
#include "../twattila/depth_levels.hlsli"

// Half resolution (line_sweep_ao.hpp): the half-size results (u1, written by the bitmask pass) and the same texture
// read by the upsample (t40, twattila/ao_upsample.hlsli).
RWTexture2D<float2> t_rw_ssao_half : register(u1);
Texture2D<float2> t_ao_half : register(t40);

// A depth sample's distance from the camera (0: nothing there, as the sky), for Half resolution.
float AoDistance(float2 uv, float depth) {
  const float distance = length(AoViewPosition(uv, depth));
  return (distance > 1e-3f && distance < 6e4f) ? distance : 0.f;
}

// A depth sample's view position, at the centre of the depth texel it read: off-centre positions with a texel's depth
// made sloped ground occlude itself in a regular dot pattern (Warhammer II). From Depth Level `level` (0: the depth).
float3 AoSamplePosition(float2 uv, float2 depth_texel, DepthLevels levels, uint level) {
  float2 texel = floor(uv / depth_texel);
  float depth;
  [branch]
  if (level == 0u) {
    depth = AoDepth((texel + 0.5f) * depth_texel);
  } else {
    texel = DepthLevelLoad(levels, level, uv / depth_texel, depth);
  }
  return AoViewPosition((texel + 0.5f) * depth_texel, depth);
}

// The visibility (1 = open) at `uv`. `region_texel`: one AO pixel in uv; `depth_texel`: one depth pixel in uv;
// `radius`: in view units (metres).
float BitmaskVisibility(float2 pixel_uv, uint2 pixel, float2 region_texel, float2 depth_texel, float radius) {
  const float2 uv = (floor(pixel_uv / depth_texel) + 0.5f) * depth_texel;
  const float depth = AoDepth(uv);
  const float3 position = AoViewPosition(uv, depth);
  const float distance = length(position);
  // The sky (also a reversed infinite far plane): open.
  if (!(distance > 1e-3f && distance < 1e5f)) return 1.f;

  // One AO pixel along x and along y at this depth in view space: the radius in AO pixels. Below one, nothing to
  // sample.
  const float3 step_x = AoViewPosition(uv + float2(region_texel.x, 0.f), depth) - position;
  const float3 step_y = AoViewPosition(uv + float2(0.f, region_texel.y), depth) - position;
  const float pixel_size = max(length(step_x), length(step_y));
  const float radius_pixels = radius / max(pixel_size, 1e-6f);
  if (!(radius_pixels >= 1.f)) return 1.f;

  // The normal from the nearer neighbour on each axis (no normals bound in these passes).
  const DepthLevels levels = DepthLevelsBound();
  const float3 right = AoSamplePosition(uv + float2(depth_texel.x, 0.f), depth_texel, levels, 0u);
  const float3 left = AoSamplePosition(uv - float2(depth_texel.x, 0.f), depth_texel, levels, 0u);
  const float3 down = AoSamplePosition(uv + float2(0.f, depth_texel.y), depth_texel, levels, 0u);
  const float3 up = AoSamplePosition(uv - float2(0.f, depth_texel.y), depth_texel, levels, 0u);
  const float3 tangent_x = (length(right - position) < length(position - left)) ? (right - position) : (position - left);
  const float3 tangent_y = (length(down - position) < length(position - up)) ? (down - position) : (position - up);
  const float3 view = -position / distance;
  float3 normal = normalize(cross(tangent_x, tangent_y));
  if (dot(normal, view) < 0.f) normal = -normal;

  // Thin surfaces: a constant thickness behind each depth sample, growing with distance (the paper's option against
  // occlusion fading far away).
  const float thickness = SI.ssao_thickness * (1.f + 0.02f * distance);
  // Interleaved gradient noise: the slices' rotation and the steps' offsets per pixel (the game's blur smooths it),
  // moved each frame with Temporal Smoothing (the includer's AO_NOISE_FRAME, 0-63: Jimenez's 5.588238 pixels a frame).
  const float2 noise_pixel = float2(pixel) + 5.588238f * AO_NOISE_FRAME;
  const float noise = frac(52.9829189f * frac(dot(noise_pixel, float2(0.06711056f, 0.00583715f))));
  // The sectors' edges jittered per slice by a second noise.
  const float jitter =
      frac(52.9829189f * frac(dot(noise_pixel + float2(47.f, 17.f), float2(0.06711056f, 0.00583715f))));
  // Samples per side of a slice: 8 x 2^Quality (8 to 256; more catch thin objects at a large radius), at most one per
  // AO pixel of the radius (more would read the same pixels).
  const uint steps = min(8u << (uint)clamp(SI.ssao_quality, 0.f, 5.f), max(4u, (uint)radius_pixels));
  // Directions (slices) around the view vector: 2^Directions.
  const uint slices = 1u << (uint)clamp(SI.ssao_directions, 0.f, 3.f);

  // The slices drawn in proportion to the projected normal: each counts the same.
  float visibility = 0.f;
  [loop]
  for (uint slice = 0; slice < slices; ++slice) {
    const BitmaskSlice slice_info = BitmaskSliceAt((slice + noise) / slices, normal, view, position);
    const float slice_jitter = frac(jitter + 0.618034f * slice);
    // On screen (one AO pixel along it): the step across x and y that stays in the slice's plane, toward +direction.
    const float3 plane = cross(view, slice_info.direction);
    float2 screen = float2(dot(step_y, plane), -dot(step_x, plane));
    if (dot(screen.x * step_x + screen.y * step_y, slice_info.direction) < 0.f) screen = -screen;
    const float2 step_uv = normalize(screen) * region_texel;
    // Auto thickness: each side's occluder, the slice's random scale.
    BitmaskOccluder occluder_minus = {position, position};
    BitmaskOccluder occluder_plus = {position, position};
    const float occluder_scale = exp2(2.f * frac(jitter + 0.5f + 0.754877666f * slice) - 1.f);

    uint occluded = 0u;
    // Until every sector is covered (nothing farther can change it).
    [loop]
    for (uint step = 0; step < steps && occluded != 0xFFFFFFFFu; ++step) {
      float t = (step + frac(noise + 0.618034f * (step + slice * steps))) / steps;
      // Denser near the pixel; at least one AO pixel away (the centre itself is no occluder).
      t = max(t * t, 1.f / radius_pixels);
      const float2 offset = step_uv * (t * radius_pixels);
      const uint level = BitmaskSampleLevel(length(offset / depth_texel), levels.count);
      [unroll]
      for (int side = -1; side <= 1; side += 2) {
        const float3 sample_position = AoSamplePosition(uv + side * offset, depth_texel, levels, level);
        float sample_thickness = thickness;
        BitmaskOccluder occluder = occluder_minus;
        if (side > 0) occluder = occluder_plus;
        [branch]
        if (SI.ssao_auto_thickness != 0.f) {
          sample_thickness = BitmaskOccluderThickness(occluder, sample_position, view, thickness, occluder_scale);
          if (side > 0) {
            occluder_plus = occluder;
          } else {
            occluder_minus = occluder;
          }
        }
        const float3 delta = sample_position - position;
        const float delta_length = length(delta);
        if (!(delta_length > 1e-4f) || delta_length > radius) continue;
        const float3 front = delta / delta_length;
        // Its thickness along its own camera ray (auto thickness: its occluder's).
        float3 back;
        [branch]
        if (SI.ssao_auto_thickness != 0.f) {
          back = BitmaskOccluderBack(occluder, delta, position, view, sample_thickness);
        } else {
          back = normalize(delta + normalize(sample_position) * thickness);
        }
        occluded |= BitmaskSectors(front, back, view, side, slice_info, slice_jitter);
      }
    }
    visibility += 1.f - countbits(occluded) / (float)BITMASK_SECTORS;
  }
  return visibility / slices;
}

#endif  // SRC_TWWARHAMMER2_BITMASK_AO_HLSLI_
