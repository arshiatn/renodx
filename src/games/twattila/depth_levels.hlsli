/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_DEPTH_LEVELS_HLSLI_
#define SRC_GAMES_TWATTILA_DEPTH_LEVELS_HLSLI_

// Ambient occlusion Depth Levels (depth_levels.hpp builds them, the addon binds them at t41 for the pass; none bound:
// the depth only): the AO's depth at 1/2 to 1/32 of its size, each texel one depth texel of its 2 x 2 (a rotated grid:
// thin lines stay in every level), its bits and its coordinate in the depth (x | y << 16). A sample far from the pixel
// reads one (BitmaskSampleLevel, bitmask.hlsli, included first), so it stays in the texture cache.
Texture2D<uint2> t_depth_levels : register(t41);

struct DepthLevels {
  uint2 size;  // the first level's (half the depth's)
  uint count;  // 0: none bound
};

DepthLevels DepthLevelsBound() {
  DepthLevels levels;
  t_depth_levels.GetDimensions(0, levels.size.x, levels.size.y, levels.count);
  return levels;
}

// Level `level` (1 or more) at `texel` (in the depth's texels): the depth texel it kept there, and its depth.
uint2 DepthLevelLoad(DepthLevels levels, uint level, float2 texel, out float depth) {
  const uint2 size = max(levels.size >> (level - 1u), 1u);
  const uint2 value = t_depth_levels.Load(int3(min(uint2(max(texel, 0.f)) >> level, size - 1u), level - 1u));
  depth = asfloat(value.x);
  return uint2(value.y & 0xFFFFu, value.y >> 16);
}

#endif  // SRC_GAMES_TWATTILA_DEPTH_LEVELS_HLSLI_
