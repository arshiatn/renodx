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
  float diffuse_rule;  // SunDraw::diffuse_rule: which rule the draw's G-buffer follows (only games with more than one read it)
  // Where in its scale x scale block the bounce's texel was taken, in pixels from the block's top-left (the apply weighs it
  // there): 0 the screen-space trace's (its geometry's top-left pixel), scale / 2 the rays' (rt_path.hlsli TexelSurface).
  float texel_centre;
  // Directional Light Bounce (Experimental, bitmask_sh.hlsli; 0: off, every pass as without it): the prepare's normal
  // the mean of its block's, the trace's light with its direction beside it, the resolve and the denoiser keep it, the
  // apply each pixel's at its own normal.
  float directional;
  float directional_form;  // 0: ZH3 with the memo's step; 1: ZH3 alone (S2's A/B)
  float transport;         // the denoiser's half transport (0-1): each tap's light moved that far toward the texel's normal
  // Directional Light Bounce's self term (shg_ design 3.1, k_self): x the texels' own light at the apply, the share of it
  // the pixel's normal sees below their trace normal's horizon, (1 - n . n0) / 2 (0: none).
  float self_light;
  float padding_0;
  float padding_1;
  float padding_2;
};


#ifndef __cplusplus
cbuffer light_bounce : register(b12) {
  LightBounceConstants LB;
}

// Directional Light Bounce's shader code: only in the passes' _sh entries, which define LIGHT_BOUNCE_DIRECTIONAL first
// (Attila's light_bounce_*_sh.ps_5_0.hlsl, light_bounce.hpp's Passes: its registers stay out of every other build, shg_
// design 4.7); elsewhere false, and every pass compiles as without it.
#if defined(LIGHT_BOUNCE_DIRECTIONAL)
#define BOUNCE_DIRECTIONAL (LB.directional > 0.f)
#else
#define BOUNCE_DIRECTIONAL false
#endif
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
