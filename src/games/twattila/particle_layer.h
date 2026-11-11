/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_PARTICLE_LAYER_H_
#define SRC_GAMES_TWATTILA_PARTICLE_LAYER_H_

// A half-size particle layer's constants (b8), set by the addon (particle_history.hpp, particle_layer.hpp): its passes'
// and, bound for the particle draws drawn into it, the particle shaders' (Attila's vfx00 reads layer_scale).
struct ParticleLayerConstants {
  float history_most;   // the share of the last frames kept at most (0: none, the history starts again)
  float camera_cut;     // metres the camera may move between two frames with the history kept
  float region_width;   // the layer's part of its textures (its viewport), in texels
  float region_height;
  float layer_scale;    // render pixels per layer texel along a side (2); unbound: 0, read as 1
  float padding[3];
};

#ifndef __cplusplus
cbuffer particle_layer : register(b8) {
  ParticleLayerConstants PL;
}
#endif

#endif  // SRC_GAMES_TWATTILA_PARTICLE_LAYER_H_
