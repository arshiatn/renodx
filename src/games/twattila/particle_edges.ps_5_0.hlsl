/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Full-Resolution Particle Edges (particle_layer.hpp; the idea of Cantlay's "High-Speed, Off-Screen Particles", GPU Gems 3
// ch. 23): the render pixels whose layer texels were drawn against other depths (a unit's, a tree's or a wall's outline:
// where ParticleUpsample weighs them by depth) marked in the addon's depth-stencil (stencil 1, the scene's depth there).
// The particle draws are drawn again at full size there and the composite leaves those pixels alone. t52 the layer's
// depth, t53 the scene's.
#include "./light_bounce.hlsli"
#include "./particle_layer.hlsli"

Texture2D<float> t_layer_depth : register(t52);
Texture2D<float> t_depth : register(t53);

float main(float4 v0 : SV_Position0) : SV_Depth {
  const int2 pixel = int2(v0.xy);
  const float depth = t_depth.Load(int3(pixel, 0));
  const float distance = length(BounceViewPositionAt(pixel, depth));
  const float4 differences = ParticleLayerDifferences(t_layer_depth, int2(floor(v0.xy / ParticleLayerScale() - 0.5f)), distance);
  if (!(max(max(differences.x, differences.y), max(differences.z, differences.w)) > PARTICLE_EDGE_SHARE * distance)) discard;
  return depth;
}
