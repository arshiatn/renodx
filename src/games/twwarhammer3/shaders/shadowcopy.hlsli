/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III, the shadow maps' half-size copies (800F7F7F: the dynamic and the kept maps into one; D5DCC85F: the dynamic
// maps for the particles' shadows). The game takes the deepest of the 2 x 2 texels at twice the target's pixel; with the maps
// at twice their size (shadow_map_size.hpp) the same part of the map is 4 x 4 texels at four times the pixel. At the game's
// size the result is the game's to the bit (test/tw_shadow_map_size).
cbuffer cb_texture_array_index : register(b0) {
  int texture_array_index;
}

// The texels of `map` under one pixel of the game's copy: 2 a side at the game's sizes (4096 and below), 4 above.
int ShadowCopyTexels(Texture2DArray<float> map) {
  uint width;
  uint height;
  uint slices;
  map.GetDimensions(width, height, slices);
  return (width > 4096u) ? 4 : 2;
}
