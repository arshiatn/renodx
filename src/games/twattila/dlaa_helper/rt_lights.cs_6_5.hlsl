/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's local light shadows (bridge v9; dlaa_helper/rt_d3d12.hpp), a pass after the trace: from
// each render pixel's world position a ray toward each light within reach (hard shadows, the rays stopping short of the
// light's own fixture). Up to 128 lights (bridge v31): each 8 x 8 tile (a group) lists the first 32 its pixels reach in
// the addon's order (by importance), bit i of light_mask the list's light i (lit or out of reach); the lists in the
// mask's rows from light_tile_row down (rt_local_lights.hlsli: tile (x, y)'s 8 words at column x * 4 + (word & 3), row
// light_tile_row + y * 2 + (word >> 2), a light's index a byte each, 0xFF past the end). light_tile_row 0 (an addon
// before v31, at most 32 lights): the lights' own order. With more lights in reach than LIGHT_RAYS_ALWAYS +
// LIGHT_RAYS_TURNS (a burning city: each light every pixel reaches cost ~0.18 ms at 4K) the list's first are traced
// every frame and the others in turns; one not traced keeps the last trace's bit where it saw the same surface
// (light_history, in the ambient's parity, its tiles' lists below it as the mask's). The harness at 4K: 16 lights in
// every pixel's reach 3.80 -> 3.13 ms, 32 lights 6.64 -> 5.17 ms, the bits the same as tracing all once the turns came
// round. Importance-Sampled Torch Shadows (LIGHT_IMPORTANCE) instead: LIGHT_IMPORTANCE_RAYS lights a pixel, picked by
// what they give it (ReSTIR DI's candidates: their colour, falloff and angle), stratified with one number a 8 x 8 block
// (its pixels refresh the same lights); the others keep their bits (dim lights' shadows follow slower). Steady Torch
// Shadows (LIGHT_STEADY, helpers 106 on) instead: every light the pixel reaches traced every frame (no turns, picks or kept
// bits: a still pixel's bits change only with the scene and the lights), hard (helpers 108 on): a bit can't hold a share of
// a light without noise that moves each frame, and helper106's fixed disk point a pixel was a fixed dither, black dots in
// every penumbra (Pharaoh's campaign, its torches' disk a quarter of their reach).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_lights.h -Vn RT_LIGHTS; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_LIGHTS_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"

RWTexture2D<uint> light_mask : register(u22);
RWTexture2D<uint> light_history[2] : register(u32);  // the bits kept over frames

static const uint LIGHT_RAYS_ALWAYS = 4u;  // the list's first, traced every frame
static const uint LIGHT_RAYS_TURNS = 4u;   // and the others' a frame
static const uint LIGHT_IMPORTANCE_RAYS = 2u;
static const uint TILE_LIGHTS = 32u;  // a tile's list: the mask's bits
static const uint NO_LIGHT = 0xFFu;

groupshared uint tile_reach[4];  // the lights any of the tile's pixels reaches (128 bits)
groupshared uint tile_list[8];   // the tile's lights, a byte each
groupshared uint tile_size;

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

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex) {
  if (index < 4u) tile_reach[index] = 0u;
  GroupMemoryBarrierWithGroupSync();
  const bool inside = id.x < width && id.y < height;
  const float4 position = inside ? positions[id.xy] : 0.f;
  const bool surface = position.w > 0.f;
  const uint count = min(light_count, 128u);
  const bool tiled = light_tile_row != 0u;
  SceneRayFlags();
  // Lights out of reach or behind a planar surface keep their bit: their own shading leaves those pixels dark (at
  // corners, cracks and edges the normal from the depth can be wrong: traced).
  float3 normal = 0.f;
  bool planar = false;
  float3 light_origin = 0.f;
  uint4 reached = 0u;
  if (surface) {
    normal = SurfaceNormal(int2(id.xy), position);
    planar = Planar(int2(id.xy), position, normal);
    light_origin = StartPast(RayOrigin(position, normal, planar), position);
    [loop]
    for (uint light = 0u; light < count; ++light) {
      const float3 to_light = lights[light].xyz - light_origin;
      const float light_distance = length(to_light);
      if (light_distance < lights[light].w && light_distance > 1.0003098e-3f && (!planar || dot(to_light, normal) > 0.f)) reached = WithBit(reached, light);
    }
    if (reached.x != 0u) InterlockedOr(tile_reach[0], reached.x);
    if (reached.y != 0u) InterlockedOr(tile_reach[1], reached.y);
    if (reached.z != 0u) InterlockedOr(tile_reach[2], reached.z);
    if (reached.w != 0u) InterlockedOr(tile_reach[3], reached.w);
  }
  GroupMemoryBarrierWithGroupSync();
  if (index == 0u) {
    [unroll]
    for (uint word = 0u; word < 8u; ++word) tile_list[word] = 0xFFFFFFFFu;
    const uint4 tile = uint4(tile_reach[0], tile_reach[1], tile_reach[2], tile_reach[3]);
    uint size = 0u;
    [loop]
    for (uint light = 0u; light < count && size < TILE_LIGHTS; ++light) {
      if (tiled && SetBit(tile, light) == 0u) continue;
      const uint shift = (size & 3u) * 8u;
      tile_list[size >> 2u] = (tile_list[size >> 2u] & ~(0xFFu << shift)) | (light << shift);
      ++size;
    }
    tile_size = size;
  }
  GroupMemoryBarrierWithGroupSync();
  const uint parity_written = (path_flags & PATH_AMBIENT_PARITY) != 0u ? 0u : 1u;
  if (tiled && index < 8u) {
    light_mask[TileWord(group.xy, index)] = tile_list[index];
    light_history[parity_written][TileWord(group.xy, index)] = tile_list[index];
  }
  if (!inside) return;
  if (!surface) {
#if defined(TORCHES_IN_BOUNCE)
    // (Torch and Fire Shadows from the Light Bounce: the words are ratios, 1 here.)
    light_mask[id.xy] = ((flags & (LIGHTS_IN_BOUNCE | LIGHT_RATIO)) == (LIGHTS_IN_BOUNCE | LIGHT_RATIO)) ? asuint(1.f) : 0xFFFFFFFFu;
#else
    light_mask[id.xy] = 0xFFFFFFFFu;
#endif
    return;
  }
  const uint size = tile_size;
  uint reached_slots = 0u;  // bit s: the pixel reaches the list's light s
  [loop]
  for (uint slot = 0u; slot < size; ++slot) {
    if (SetBit(reached, ListLight(slot)) != 0u) reached_slots |= 1u << slot;
  }
#if defined(TORCHES_IN_BOUNCE)
  // Torch and Fire Light in the Light Bounce (only this TORCHES_IN_BOUNCE build: the other is helper110's to the byte):
  // the paths' lights dark for the game's light shaders, untraced (their light, shadowed, comes with the bounce); their
  // kept bits stay lit (never read: the helper drops this history at a switch).
  uint bounce_slots = 0u;
  if ((flags & LIGHTS_IN_BOUNCE) != 0u) {
    [loop]
    for (uint slot = 0u; slot < size; ++slot) {
      if (((reached_slots >> slot) & 1u) != 0u && ListLight(slot) < BOUNCE_LIGHTS) bounce_slots |= 1u << slot;
    }
    reached_slots &= ~bounce_slots;
  }
  // Torch and Fire Shadows from the Light Bounce: no rays; the word is the last bounce's shadow ratio at this surface (its
  // texels around where the last trace saw it, by the bounce apply's weights: near, and alike in depth), 1 where nothing
  // was seen (the game's light unshadowed for that frame).
  if ((flags & (LIGHTS_IN_BOUNCE | LIGHT_RATIO)) == (LIGHTS_IN_BOUNCE | LIGHT_RATIO)) {
    float ratio = 1.f;
    uint2 seen_pixel;
    if (bounce_slots != 0u && (lights_in_bounce & LIGHTS_RATIO_VALID) != 0u && (path_flags & PATH_AMBIENT_HISTORY) != 0u && bounce_scale != 0u
        && SeenByLastTrace(position.xyz, seen_pixel)) {
      const uint read_set = ((lights_in_bounce & LIGHTS_RATIO_SET) != 0u) ? 1u : 0u;
      const float seen_depth = ambient_positions[seen_pixel].w;
      const int2 center = int2(seen_pixel / bounce_scale);
      float2 sum = 0.f;
      [unroll]
      for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
          const int2 texel = center + int2(x, y);
          if (any(texel < 0) || texel.x >= (int)bounce_width || texel.y >= (int)bounce_height) continue;
          const float4 kept = light_ratio[read_set][texel];
          const float2 offset = float2((texel * (int)bounce_scale) - int2(seen_pixel)) / (float)bounce_scale;
          const float similar = saturate(1.f - abs(kept.w - seen_depth) / (0.030001808f * seen_depth));
          sum += kept.xy * ((kept.w > 0.f) ? exp2(-0.25f * dot(offset, offset)) * similar : 0.f);
        }
      }
      if (sum.y > 1e-8f) ratio = saturate(sum.x / sum.y);
    }
    light_mask[id.xy] = asuint(ratio);
    light_history[parity_written][id.xy] = 0xFFFFFFFFu;
    return;
  }
#endif
  uint traced = reached_slots;
  uint lit = 0xFFFFFFFFu;
  uint2 seen_pixel;
  const uint reached_count = countbits(reached_slots);
  const bool importance = (flags & LIGHT_IMPORTANCE) != 0u;
  const bool steady = (flags & LIGHT_STEADY) != 0u;
  if (!steady && reached_count > (importance ? LIGHT_IMPORTANCE_RAYS : LIGHT_RAYS_ALWAYS + LIGHT_RAYS_TURNS)
      && (path_flags & PATH_LIGHT_HISTORY) != 0u && SeenByLastTrace(position.xyz, seen_pixel)) {
    // The list's first LIGHT_RAYS_ALWAYS every frame, the others in turns by their place, each 8 x 8 block its own phase
    // (a light's shadows change a block at a time); a light new to the list, or not in the last trace's list where it saw
    // the surface, is traced.
    const uint parity_read = 1u - parity_written;
    const uint kept_bits = light_history[parity_read][seen_pixel];
    // The last trace's lights there (by their place in its whole list: light_previous) and those it found blocked.
    uint4 known = uint4(0xFFFFFFFFu, 0u, 0u, 0u);
    uint4 dark = uint4(~kept_bits, 0u, 0u, 0u);
    if (tiled) {
      known = 0u;
      dark = 0u;
      [loop]
      for (uint word = 0u; word < 8u; ++word) {
        const uint kept_lights = light_history[parity_read][TileWord(seen_pixel >> 3u, word)];
        [unroll]
        for (uint byte = 0u; byte < 4u; ++byte) {
          const uint previous = (kept_lights >> (byte * 8u)) & 0xFFu;
          if (previous == NO_LIGHT) continue;
          known = WithBit(known, previous);
          if (((kept_bits >> ((word * 4u) + byte)) & 1u) == 0u) dark = WithBit(dark, previous);
        }
      }
    }
    const uint turns = max((size + LIGHT_RAYS_TURNS - 1u - min(size, LIGHT_RAYS_ALWAYS)) / LIGHT_RAYS_TURNS, 1u);
    const uint phase = frame + (id.x >> 3) + ((id.y >> 3) * 7u);
    uint chosen = 0u;
    if (importance) {
      // Systematic picks along the lights' weights (a light that gives more than one step takes one ray).
      float total = 0.f;
      [loop]
      for (uint slot = 0u; slot < size; ++slot) {
        float3 direction;
        float light_distance;
        if (((reached_slots >> slot) & 1u) != 0u) total += Luminance(LightArriving(ListLight(slot), light_origin, normal, direction, light_distance));
      }
      const float step = total / (float)LIGHT_IMPORTANCE_RAYS;
      float next = Random(uint3(id.xy >> 3u, frame * 4u + 7u)) * step;
      float running = 0.f;
      [loop]
      for (uint slot = 0u; slot < size && total > 0.f; ++slot) {
        float3 direction;
        float light_distance;
        if (((reached_slots >> slot) & 1u) == 0u) continue;
        running += Luminance(LightArriving(ListLight(slot), light_origin, normal, direction, light_distance));
        if (running > next) {
          chosen |= 1u << slot;
          next += step * ceil((running - next) / step);
        }
      }
    }
    uint kept_dark = 0u;  // the others the last trace found blocked
    [loop]
    for (uint slot = 0u; slot < size; ++slot) {
      // Its byte (selected, not indexed).
      const uint light = ListLight(slot);
      const uint4 words = light_previous[light >> 4u];
      const uint lane = (light >> 2u) & 3u;
      const uint previous = (((lane == 0u) ? words.x : (lane == 1u) ? words.y : (lane == 2u) ? words.z : words.w) >> ((light & 3u) * 8u)) & 0xFFu;
      if (previous == NO_LIGHT || SetBit(known, previous) == 0u || (!importance && (slot < LIGHT_RAYS_ALWAYS || (slot + phase) % turns == 0u))) {
        chosen |= 1u << slot;
      } else if (((chosen >> slot) & 1u) == 0u && SetBit(dark, previous) != 0u) {
        kept_dark |= 1u << slot;
      }
    }
    traced = reached_slots & chosen;
    lit = ~(reached_slots & kept_dark);
  }
  // Soft shadows under Ray Reconstruction: each ray to a point of the light's disk (light_size), the bit lit where the
  // share of rays that reached it beats noise moved each frame, which RR averages into the share (hard: half). Several
  // rays a pixel (Ray Tracing at Output Resolution) each from its own spot of a planar pixel.
  // Steady: each ray to the light's centre, most of a pixel's rays decide.
  const bool reconstructed = (flags & RECONSTRUCTED) != 0u;
  const bool soft = reconstructed && light_size > 0.f && !steady;
  const uint rays = clamp(shadow_rays, 1u, 8u);
  const float threshold = (reconstructed && !steady && (soft || rays > 1u)) ? InterleavedGradientNoise(float2(id.xy)) : 0.5f;
  const float turn = InterleavedGradientNoise(float2(id.xy) + 37.f);
  [loop]
  for (uint slot = 0u; slot < size; ++slot) {
    if (((traced >> slot) & 1u) == 0u) continue;
    const uint light = ListLight(slot);
    uint reached_rays = 0u;
    [loop]
    for (uint ray = 0u; ray < rays; ++ray) {
      const float3 from = light_origin + ((rays > 1u && planar) ? PixelSpot(int2(id.xy), SpotOf(ray, turn)) : 0.f);
      float3 to_light = lights[light].xyz - from;
      if (soft) {
        const uint seed = ((frame * 128u) + light) * 16u + ray * 2u;
        to_light += DiskPoint(normalize(to_light), light_size, float2(Random(uint3(id.xy, seed)), Random(uint3(id.xy, seed + 1u))));
      }
      const float light_distance = length(to_light);
      if (!Occluded(from, to_light / light_distance, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS_SMOKE)) ++reached_rays;
    }
    if ((float)reached_rays <= threshold * (float)rays) lit &= ~(1u << slot);
  }
#if defined(TORCHES_IN_BOUNCE)
  light_mask[id.xy] = lit & ~bounce_slots;
#else
  light_mask[id.xy] = lit;
#endif
  light_history[parity_written][id.xy] = lit;
}
