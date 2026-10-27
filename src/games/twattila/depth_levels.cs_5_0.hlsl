/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ambient occlusion Depth Levels (depth_levels.hpp): one level (u0, 8 x 8 groups over it) at half the size of the one
// above, each texel one of its 2 x 2 (a rotated grid, alternating per row and column, so thin lines stay in every
// level) as that depth's bits and its texel's coordinate in the game's depth (x | y << 16: the ambient occlusion places
// the sample there, on the surface). The first level from the game's depth (t0), the others from the level above (t1,
// none bound for the first).
Texture2D<float4> game_depth : register(t0);
Texture2D<uint2> level_above : register(t1);
RWTexture2D<uint2> level : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  level.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const uint2 picked = id.xy * 2u + uint2(id.y & 1u, id.x & 1u);
  uint width, height, levels;
  level_above.GetDimensions(0, width, height, levels);
  [branch]
  if (levels == 0u) {
    game_depth.GetDimensions(width, height);
    const uint2 source = min(picked, uint2(width, height) - 1u);
    level[id.xy] = uint2(asuint(game_depth.Load(int3(source, 0)).x), source.x | (source.y << 16));
  } else {
    level[id.xy] = level_above.Load(int3(min(picked, uint2(width, height) - 1u), 0));
  }
}
