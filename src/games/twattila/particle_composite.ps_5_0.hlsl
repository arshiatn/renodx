/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Particles at Half Resolution (Experimental), the composite (particle_layer.hpp): the half-size layer (t50: rgb the
// particles' light, alpha the share of the scene they leave; smoothed with Smooth Half-Size Particles) brought up to each
// render pixel by depth (particle_layer.hlsli: the layer's depth t52, the scene's t53), over the light buffer (blend: one,
// source alpha).
#include "./light_bounce.hlsli"
#include "./particle_layer.hlsli"

Texture2D<float4> t_layer : register(t50);
Texture2D<float> t_layer_depth : register(t52);
Texture2D<float> t_depth : register(t53);

float4 main(float4 v0 : SV_Position0) : SV_Target0 {
  const int2 pixel = int2(v0.xy);
  const float distance = length(BounceViewPositionAt(pixel, t_depth.Load(int3(pixel, 0))));
  int2 base;
  const float4 weights = ParticleUpsample(t_layer_depth, v0.xy / PARTICLE_LAYER_SCALE - 0.5f, distance, base);
  return ParticleGather(t_layer, base, weights);
}
