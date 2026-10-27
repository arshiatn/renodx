/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_TWWARHAMMER2_AO_BITMASK_BLUR_HLSLI_
#define SRC_TWWARHAMMER2_AO_BITMASK_BLUR_HLSLI_

// Ambient Occlusion: Visibility Bitmask in place of the line-sweep AO's horizontal blur (Warhammer, Troy, Pharaoh;
// line_sweep_ao.hpp dispatches it, 8 x 8 groups, with the blur's own bindings and the addon's settings at b13):
// bitmask_ao.hlsli from the blur's depth and camera, over the part of the depth texture the game's blur reads (its
// output's size), then a 5 x 5 depth-aware average inside the group; the game's vertical blur follows. Half resolution
// (SI.ssao_resolution): over half the size (each pixel the top-left one of a 2 x 2) into t_rw_ssao_half, then the
// AO_UPSAMPLE build over the output. Include after the game's shared.h.
cbuffer camera : register(b0) {
  float4x4 inv_projection : packoffset(c17);
}

Texture2D<float> gbuffer_depth_texture : register(t0);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

static const uint GROUP_SIZE = 8;
static const uint TILE_SIZE = GROUP_SIZE + 4;  // the group's pixels and a 2-pixel border
static float2 ao_depth_size;

float AoDepth(float2 uv) {
  return gbuffer_depth_texture.Load(int3(min(uint2(saturate(uv) * ao_depth_size), uint2(ao_depth_size) - 1u), 0));
}

float3 AoViewPosition(float2 uv, float depth) {
  const float4 view = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_projection);
  return view.xyz / view.w;
}

#include "./bitmask_ao.hlsli"

groupshared float tile_ao[TILE_SIZE * TILE_SIZE];
groupshared float tile_depth[TILE_SIZE * TILE_SIZE];

#ifdef AO_UPSAMPLE
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  uint2 size;
  t_rw_ssao.GetDimensions(size.x, size.y);
  if (any(coords.xy >= size)) return;
  ao_depth_size = float2(size);
  const float2 uv = (coords.xy + 0.5f) / ao_depth_size;
  const float distance = AoDistance(uv, gbuffer_depth_texture.Load(int3(coords.xy, 0)));
  t_rw_ssao[coords.xy] = (distance > 0.f) ? AoUpsample(t_ao_half, int2(coords.xy), distance, 2.f) : 1.f;
}
#else
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 coords : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID,
          uint thread_index : SV_GroupIndex) {
  uint2 size;
  t_rw_ssao.GetDimensions(size.x, size.y);
  ao_depth_size = float2(size);
  const float2 texel = 1.f / ao_depth_size;
  // Half resolution: this dispatch covers half the size, each of its pixels the top-left one of a 2 x 2.
  const uint scale = (SI.ssao_resolution != 0.f) ? 2u : 1u;
  const uint2 ao_size = (size + scale - 1u) / scale;
  for (uint cell = thread_index; cell < TILE_SIZE * TILE_SIZE; cell += GROUP_SIZE * GROUP_SIZE) {
    const int2 ao_pixel = int2(group.xy * GROUP_SIZE + uint2(cell % TILE_SIZE, cell / TILE_SIZE)) - 2;
    const int2 pixel = ao_pixel * int(scale);
    float visibility = 1.f;
    float depth = 0.f;
    [branch]
    if (all(ao_pixel >= 0) && all(ao_pixel < int2(ao_size))) {
      depth = gbuffer_depth_texture.Load(int3(pixel, 0));
      visibility = BitmaskVisibility((pixel + 0.5f) * texel, uint2(ao_pixel), texel, texel, SI.ssao_radius);
    }
    tile_ao[cell] = visibility;
    tile_depth[cell] = depth;
  }
  GroupMemoryBarrierWithGroupSync();
  if (any(coords.xy >= ao_size)) return;
  // The noise's 4 slice rotations spread over neighbours: averaged, as the game's collect averages its directions.
  const uint center = (thread.y + 2u) * TILE_SIZE + thread.x + 2u;
  float sum = 0.f;
  float sum_weight = 0.f;
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      const int2 position = int2(coords.xy) + int2(x, y);
      if (any(position < 0) || any(position >= int2(ao_size))) continue;
      const uint cell = uint(int(center) + y * int(TILE_SIZE) + x);
      const float weight = 1.f / (1.f + 1000.f * abs(tile_depth[center] - tile_depth[cell]));
      sum += weight * tile_ao[cell];
      sum_weight += weight;
    }
  }
  const float value = exp2(log2(max(sum / sum_weight, 1e-4f)) * SI.ssao_strength);
  [branch]
  if (scale > 1u) {
    t_rw_ssao_half[coords.xy] = float2(value, AoDistance((coords.xy * scale + 0.5f) * texel, tile_depth[center]));
  } else {
    t_rw_ssao[coords.xy] = value;
  }
}
#endif

#endif  // SRC_TWWARHAMMER2_AO_BITMASK_BLUR_HLSLI_
