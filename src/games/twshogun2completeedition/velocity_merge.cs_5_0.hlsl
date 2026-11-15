/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's object velocity (object_velocity.hpp): the game's motion vectors (t0: pixels, this frame's position minus
// last frame's, y up: gbuffer.hlsl calc_motion_vector) with the object motion (t1: motion_wind.ps's MotionTexel, pixels
// to where the surface was last frame, x right, y down, and its depth) where that is the final surface (t2: the depth
// DLSS gets), into the motion vectors DLSS gets (u0).
#include "./motion.hlsli"

Texture2D<float2> t_game_velocity : register(t0);
Texture2D<uint2> t_object_motion : register(t1);
Texture2D<float> t_depth : register(t2);
RWTexture2D<float2> u_velocity : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  uint2 size;
  u_velocity.GetDimensions(size.x, size.y);
  if (any(pixel >= size)) return;
  const uint2 object_motion = t_object_motion.Load(int3(pixel, 0));
  float2 velocity = t_game_velocity.Load(int3(pixel, 0));
  if (MotionTexelWritten(object_motion) && MotionDepthMatches(MotionTexelDepth(object_motion), t_depth.Load(int3(pixel, 0)))) {
    const float2 motion = MotionTexelMotion(object_motion);
    velocity = float2(-motion.x, motion.y);
  }
  u_velocity[pixel] = velocity;
}
