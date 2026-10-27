/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_LIGHT_BOUNCE_H_
#define SRC_GAMES_TWATTILA_LIGHT_BOUNCE_H_

// Light Bounce (Experimental): the constants of its passes (b12), set by the addon (light_bounce.hpp).
struct LightBounceConstants {
  float strength;   // x the bounced light (0: off)
  float radius;     // metres
  float thickness;  // metres behind each depth sample (the ambient occlusion's Thickness)
  float steps;      // samples per side of a slice
  float slices;     // directions around the view vector
  float scale;      // a trace texel covers scale x scale pixels (1 full, 2 half, 4 quarter, 8 eighth resolution)
  float history;    // share of the last frames' bounce kept (0: none, the noise the same every frame)
  float frame;      // the noise's frame, 0-63
  float bounces;    // Multi-Bounce: the last frame's bounce x albedo the prepare adds to the light, its share (0: off)
  float checkerboard;  // 1: with history, every other 16 x 16 block of texels traced each frame
  float backface;        // 1: light from a surface's back side rejected (GT-VBGI's option)
  float auto_thickness;  // 1: each occluder as thick as it is wide (bitmask.hlsli)
  // The view's part of the prepared textures (the DLSS render scale draws it into their top-left; the rest holds older
  // frames): its size in level-0 texels (0: all of them), and level 0's width (each level's index from its width).
  float region_width;
  float region_height;
  float level0_width;
  float padding;
};


#ifndef __cplusplus
cbuffer light_bounce : register(b12) {
  LightBounceConstants LB;
}
#endif

#ifndef __cplusplus
// The view's part of the prepared texture `level` (`size`: that level's size): its last texel.
uint2 BounceRegionLast(uint level, uint2 size) {
  uint2 region = size;
  if (LB.region_width > 0.f) {
    const uint2 view = uint2(LB.region_width, LB.region_height);
    region = min(size, (view + (1u << level) - 1u) >> level);
  }
  return max(region, 1u) - 1u;
}
#endif

#endif  // SRC_GAMES_TWATTILA_LIGHT_BOUNCE_H_
