/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The local lights' 8 x 8 tile lists (rt_lights.cs_6_5.hlsl, a group a tile; unified PT's U2 calls the same from its
// primary pass): the lights any of the tile's pixels reaches, the list of the first TILE_LIGHTS of them in the lights'
// order (without tiles, light_tile_row 0: the first TILE_LIGHTS lights), and with LIGHTS_DI the list as a 128-bit set
// (Path-Traced Torch Light's membership: an unlisted light keeps the game's unshadowed light, so RTXDI's target is 0
// for it). The group's barriers are the caller's: TileClear, barrier, TileReach, barrier, TileList, barrier.
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_LIGHT_TILES_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_LIGHT_TILES_HLSLI_

static const uint TILE_LIGHTS = 32u;  // a tile's list: the mask's bits
static const uint NO_LIGHT = 0xFFu;

groupshared uint tile_reach[4];  // the lights any of the tile's pixels reaches (128 bits)
groupshared uint tile_list[8];   // the tile's lights, a byte each
groupshared uint tile_size;
#if defined(LIGHTS_DI)
groupshared uint tile_members[4];  // the listed lights (128 bits)
#endif

// Bit `index` of a 128-bit set, and the set with it (selected, not indexed: a vector indexed by a variable goes through
// local memory).
uint SetBit(uint4 set, uint index) {
  const uint word = index >> 5u;
  return (((word == 0u) ? set.x : (word == 1u) ? set.y : (word == 2u) ? set.z : set.w) >> (index & 31u)) & 1u;
}
uint4 WithBit(uint4 set, uint index) {
  const uint word = index >> 5u;
  const uint bit = 1u << (index & 31u);
  return set | uint4((word == 0u) ? bit : 0u, (word == 1u) ? bit : 0u, (word == 2u) ? bit : 0u, (word == 3u) ? bit : 0u);
}

// The tile's list's light at `slot`.
uint ListLight(uint slot) {
  return (tile_list[slot >> 2u] >> ((slot & 3u) * 8u)) & 0xFFu;
}

// Word `word` of tile `tile`'s list in the mask and the history.
uint2 TileWord(uint2 tile, uint word) {
  return uint2((tile.x * 4u) + (word & 3u), light_tile_row + (tile.y * 2u) + (word >> 2u));
}

// The group's sets empty (`index` the thread's in the group).
void TileClear(uint index) {
  if (index < 4u) tile_reach[index] = 0u;
  if (index < 8u) tile_list[index] = 0xFFFFFFFFu;
#if defined(LIGHTS_DI)
  if (index < 4u) tile_members[index] = 0u;
#endif
}

// A pixel's reached lights into the tile's.
void TileReach(uint4 reached) {
  if (reached.x != 0u) InterlockedOr(tile_reach[0], reached.x);
  if (reached.y != 0u) InterlockedOr(tile_reach[1], reached.y);
  if (reached.z != 0u) InterlockedOr(tile_reach[2], reached.z);
  if (reached.w != 0u) InterlockedOr(tile_reach[3], reached.w);
}

// The tile's list: the first TILE_LIGHTS of the lights it reaches (without tiles every light) in the lights' order, a
// light's slot the count of those before it. Every thread places two (lights index and index + 64) into their bytes
// (one thread's loop over all of them kept 63 waiting at the barrier); with LIGHTS_DI each listed one into the members.
void TileList(uint index, uint count, bool tiled) {
  const uint4 tile = uint4(tile_reach[0], tile_reach[1], tile_reach[2], tile_reach[3]);
  if (index == 0u) {
    tile_size = min(tiled ? (countbits(tile.x) + countbits(tile.y) + countbits(tile.z) + countbits(tile.w)) : count, TILE_LIGHTS);
  }
  [unroll]
  for (uint pair = 0u; pair < 2u; ++pair) {
    const uint light = index + (pair * 64u);
    if (light >= count || (tiled && SetBit(tile, light) == 0u)) continue;
    const uint word = light >> 5u;
    const uint before = countbits(((word == 0u) ? tile.x : (word == 1u) ? tile.y : (word == 2u) ? tile.z : tile.w) & ((1u << (light & 31u)) - 1u))
                        + ((word > 0u) ? countbits(tile.x) : 0u) + ((word > 1u) ? countbits(tile.y) : 0u) + ((word > 2u) ? countbits(tile.z) : 0u);
    const uint slot = tiled ? before : light;
    if (slot >= TILE_LIGHTS) continue;
    const uint shift = (slot & 3u) * 8u;
    InterlockedAnd(tile_list[slot >> 2u], (light << shift) | ~(0xFFu << shift));
#if defined(LIGHTS_DI)
    InterlockedOr(tile_members[word], 1u << (light & 31u));
#endif
  }
}

#if defined(LIGHTS_DI) || defined(PATH_DI)
// Path-Traced Torch Light (PT DI step 4c): each tile's listed lights (tile_members, rt_lights' LIGHTS_DI build), read by the
// DI passes (rt_rtxdi_bridge.hlsli's PATH_DI part).
RWTexture2D<uint4> di_members : register(u77);
// The light history's word at a pixel the DI passes light (rt_lights): no exact pixel's (at most LIGHT_RAYS_ALWAYS of its
// bits 0).
static const uint DI_PIXEL_WORD = 0u;

// Whether this frame's light can be the game's formula: the sun texture's texel 2 w holds the game's HDR state (0.004:
// HDR, 1: its SDR constants; 0: not written yet, the first frame or a host without it: no DI, tw_point_light.hlsli).
bool DiShadedFrame() {
  const float diffuse_scale = sun[uint2(SUN_LIGHT_TEXEL, 0)].w;
  return diffuse_scale > 0.f && diffuse_scale <= 1.f;
}
#endif

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_LIGHT_TILES_HLSLI_
