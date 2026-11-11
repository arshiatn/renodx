/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_PARTICLE_LAYER_HLSLI_
#define SRC_GAMES_TWATTILA_PARTICLE_LAYER_HLSLI_

// A half-size particle layer (Warhammer III's own, Attila's particle_layer.hpp) brought up to the render pixels: each pixel's
// share of the 2 x 2 layer texels around it, by depth. Game-neutral: the includer's light_bounce.hlsli first (its camera at
// b11: BounceViewPositionAt).
#include "./particle_layer.h"

static const int PARTICLE_LAYER_SCALE = 2;

// The distance from the camera of what layer texel `texel` was drawn against: the depth there, the farthest of its 2 x 2
// render pixels, at the first of them.
float ParticleLayerDistance(Texture2D<float> layer_depth, int2 texel) {
  return length(BounceViewPositionAt(texel * PARTICLE_LAYER_SCALE, layer_depth.Load(int3(texel, 0))));
}

// The 2 x 2 layer texels around `coordinate` (in layer texels, a texel's centre at a whole number) for a render pixel `distance`
// from the camera, in the order (0, 0), (1, 0), (0, 1), (1, 1) from `base`: bilinear where all four were drawn against about
// the pixel's own depth (the game's look), else each by how near its depth is (the rule of Warhammer III's own filtered
// composite, 1EF17B33: where one differs by more than 10%, 1 / (the difference + 1 mm)), so smoke behind a soldier stays off
// his edge and smoke in front of him stays on it.
float4 ParticleUpsample(Texture2D<float> layer_depth, float2 coordinate, float distance, out int2 base) {
  base = int2(floor(coordinate));
  const float2 blend = coordinate - float2(base);
  const float4 bilinear = float4((1.f - blend.x) * (1.f - blend.y), blend.x * (1.f - blend.y), (1.f - blend.x) * blend.y,
                                 blend.x * blend.y);
  const int2 last = int2(PL.region_width, PL.region_height) - 1;
  float4 differences;
  [unroll]
  for (int tap = 0; tap < 4; ++tap) {
    const int2 texel = clamp(base + int2(tap & 1, tap >> 1), 0, last);
    differences[tap] = abs(ParticleLayerDistance(layer_depth, texel) - distance);
  }
  if (!(max(max(differences.x, differences.y), max(differences.z, differences.w)) > 0.1f * distance)) return bilinear;
  const float4 near = 1.f / (differences + 0.001f);
  const float total = dot(near, 1.f);
  return (total > 0.f) ? (near / total) : bilinear;
}

// `layer` at the 2 x 2 texels from `base` by `weights` (ParticleUpsample's).
float4 ParticleGather(Texture2D<float4> layer, int2 base, float4 weights) {
  const int2 last = int2(PL.region_width, PL.region_height) - 1;
  float4 sum = 0.f;
  [unroll]
  for (int tap = 0; tap < 4; ++tap) {
    sum += layer.Load(int3(clamp(base + int2(tap & 1, tap >> 1), 0, last), 0)) * weights[tap];
  }
  return sum;
}

#endif  // SRC_GAMES_TWATTILA_PARTICLE_LAYER_HLSLI_
