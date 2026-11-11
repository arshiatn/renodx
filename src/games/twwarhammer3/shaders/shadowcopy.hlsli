/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III, the shadow maps' half-size copies (800F7F7F: the dynamic and the kept maps into one; D5DCC85F: the dynamic
// maps for the particles' shadows). The game takes the deepest of the 2 x 2 texels at twice the target's pixel; with the maps
// at twice their size (shadow_map_size.hpp) the same part of the map is 4 x 4 texels at four times the pixel. At the game's
// size the result is the game's to the bit (test/tw_shadow_map_size). Made smaller (Shadow Maps Under Rays, 2048 or 1024 under
// the game's 2048 and 1024 copies) one texel at the pixel's place.
#include "../shared.h"

cbuffer cb_texture_array_index : register(b0) {
  int texture_array_index;
}

bool ShadowMapsSmaller() {
  return shader_injection.shadow_map_scale > 0.f && shader_injection.shadow_map_scale < 1.f;
}

// The texels of `map` under one pixel of the game's copy: 2 a side at the game's sizes (4096 and below), 4 above, 1 smaller.
int ShadowCopyTexels(Texture2DArray<float> map) {
  uint width;
  uint height;
  uint slices;
  map.GetDimensions(width, height, slices);
  return ShadowMapsSmaller() ? 1 : ((width > 4096u) ? 4 : 2);
}

// The first of them.
int2 ShadowCopyFirst(float2 position, int texels) {
  return ShadowMapsSmaller() ? int2(floor(position * (2.f * shader_injection.shadow_map_scale))) : int2(floor(position)) * texels;
}
