/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_TWWARHAMMER3_LINE_SWEEP_BITMASK_HLSLI_
#define SRC_TWWARHAMMER3_LINE_SWEEP_BITMASK_HLSLI_

// Ambient Occlusion: Visibility Bitmask in a line-sweep collect (Warhammer III's fx_ssao, 8 x 8 groups; Warhammer II's,
// 32 x 32): Warhammer II's bitmask_ao.hlsli from the AO's depth (the includer's ao_depth, any size) and the camera the
// addon binds (Warhammer III: at b1, the last tonemap's), over the group's pixels and a 1-pixel border, then averaged
// over 3 x 3 with the game's depth weights. Half resolution (SI.ssao_resolution, twwarhammer2/line_sweep_ao.hpp): over
// half the size (each pixel the top-left one of a 2 x 2) into t_rw_ssao_half, then BitmaskUpsample over the output.
// Include after the game's shared.h, ao_depth and t_rw_ssao; Warhammer II declares ao_inv_projection itself
// (LINE_SWEEP_OWN_CAMERA).
#ifndef LINE_SWEEP_OWN_CAMERA
cbuffer ao_camera : register(b1) {
  float4x4 ao_inv_projection : packoffset(c26);
}
#endif
#ifndef LINE_SWEEP_GROUP_SIZE
#define LINE_SWEEP_GROUP_SIZE 8
#endif

static const uint GROUP_SIZE = LINE_SWEEP_GROUP_SIZE;
static const uint TILE_SIZE = GROUP_SIZE + 2;
static float2 ao_depth_size;

// `size`: the output's. Warhammer III: the sweep's depth holds its pixels one to one, in a corner of the display's depth
// below about 2/3 scale; elsewhere the depth is whole.
void SetAoDepthSize(uint2 size) {
#ifdef LINE_SWEEP_DEPTH_AT_OUTPUT_SIZE
  ao_depth_size = float2(size);
#else
  ao_depth.GetDimensions(ao_depth_size.x, ao_depth_size.y);
#endif
}

float AoDepth(float2 uv) {
  return ao_depth.Load(int3(min(uint2(saturate(uv) * ao_depth_size), uint2(ao_depth_size) - 1u), 0));
}

float3 AoViewPosition(float2 uv, float depth) {
  const float4 view = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), ao_inv_projection);
  return view.xyz / view.w;
}

#include "../twwarhammer2/bitmask_ao.hlsli"

groupshared float tile_ao[TILE_SIZE * TILE_SIZE];
groupshared float tile_depth[TILE_SIZE * TILE_SIZE];

// The game's collect weight of a depth sample against the pixel's.
float WeightDepth(float center_depth, float depth) {
  return 1.f / (1.f + 1000.f * abs(center_depth - depth));
}

// `size`: the collect's output (Warhammer III: its line LUT's size x the overscan, the depth's size).
void BitmaskCollect(uint2 coords, uint2 group, uint2 thread, uint thread_index, uint2 size) {
  SetAoDepthSize(size);
  const float2 texel = 1.f / float2(size);
  const float2 depth_texel = 1.f / ao_depth_size;
  // Half resolution: this dispatch covers half the size, each of its pixels the top-left one of a 2 x 2.
  const uint scale = (SI.ssao_resolution != 0.f) ? 2u : 1u;
  const uint2 ao_size = (size + scale - 1u) / scale;
  for (uint cell = thread_index; cell < TILE_SIZE * TILE_SIZE; cell += GROUP_SIZE * GROUP_SIZE) {
    const int2 ao_pixel = int2(group * GROUP_SIZE + uint2(cell % TILE_SIZE, cell / TILE_SIZE)) - 1;
    const int2 pixel = ao_pixel * int(scale);
    float visibility = 1.f;
    float depth = 0.f;
    [branch]
    if (all(ao_pixel >= 0) && all(ao_pixel < int2(ao_size))) {
      depth = AoDepth((pixel + 0.5f) * texel);
      visibility = BitmaskVisibility((pixel + 0.5f) * texel, uint2(ao_pixel), texel, depth_texel, SI.ssao_radius);
    }
    tile_ao[cell] = visibility;
    tile_depth[cell] = depth;
  }
  GroupMemoryBarrierWithGroupSync();
  if (any(coords >= ao_size)) return;
  // The noise's 4 slice rotations spread over neighbours: averaged, as the game's collect averages its directions.
  const uint center = (thread.y + 1u) * TILE_SIZE + thread.x + 1u;
  float sum = 0.f;
  float sum_weight = 0.f;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      const int2 position = int2(coords) + int2(x, y);
      if (any(position < 0) || any(position >= int2(ao_size))) continue;
      const uint cell = uint(int(center) + y * int(TILE_SIZE) + x);
      const float weight = WeightDepth(tile_depth[center], tile_depth[cell]);
      sum += weight * tile_ao[cell];
      sum_weight += weight;
    }
  }
  const float value = exp2(log2(max(sum / sum_weight, 1e-4f)) * SI.ssao_strength);
  [branch]
  if (scale > 1u) {
    t_rw_ssao_half[coords] = float2(value, AoDistance((coords * scale + 0.5f) * texel, tile_depth[center]));
  } else {
    t_rw_ssao[coords] = value;
  }
}

// Half resolution: the output from the half-size results (twattila/ao_upsample.hlsli); the sky open.
void BitmaskUpsample(uint2 coords, uint2 size) {
  if (any(coords >= size)) return;
  SetAoDepthSize(size);
  const float2 uv = (coords + 0.5f) / float2(size);
  const float distance = AoDistance(uv, AoDepth(uv));
  t_rw_ssao[coords] = (distance > 0.f) ? AoUpsample(t_ao_half, int2(coords), distance, 2.f) : 1.f;
}

#endif  // SRC_TWWARHAMMER3_LINE_SWEEP_BITMASK_HLSLI_
