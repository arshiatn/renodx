/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced local light shadows in the games' light passes (rt_core.hpp BindLocalLights; the helper's rt_trace traces
// them at the render pixels): t42 the lit bits of this frame's traced lights, b9 their world positions and count (b12 is
// the render scale's: render_scale.hlsli). Up to 128 lights (bridge v31): each 8 x 8 tile of t42 traces its own 32 (its
// list in t42's rows from b9's z down: tile (x, y)'s 8 words at column x * 4 + (word & 3), row z + y * 2 + (word >> 2),
// a light's index a byte each in the list's order, 0xFF past its end) and the pixel's bits are the list's order. z 0
// (screen-space torch shadows): the bits are the first 32 lights' own. A light not traced at the pixel, or nothing
// bound, stays as the game draws it.
#ifndef SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
#define SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_

Texture2D<uint> t_rt_light_mask : register(t42);
cbuffer rt_local_lights : register(b9) {
  float4 rt_light_positions[128];  // xyz, w the reach (metres)
  uint4 rt_light_count;            // x; y the glow (asfloat: torch and fire light in fog and smoke, 0 none); z the tiles' row; w 1: the mask holds shadow ratios
  float4 rt_light_colours[128];    // rgb
};

// The local lights' light reaching a world point (no shadows): each one's colour times the games' smooth falloff
// (1 - distance / reach)^2, times the glow setting. Torches and fires lighting the smoke and dust near them (vfx00): the
// 32 brightest.
float3 RtLocalLightAt(float3 world) {
  float3 light = 0.f;
  [loop]
  for (uint index = 0u; index < min(rt_light_count.x, 32u); ++index) {
    const float falloff = saturate(1.f - (distance(rt_light_positions[index].xyz, world) / max(rt_light_positions[index].w, 1.0002184e-3f)));
    light += rt_light_colours[index].rgb * (falloff * falloff);
  }
  return light * asfloat(rt_light_count.y);
}

// The traced light nearest a drawn one stands for it within this distance (metres): the traced positions are a few
// frames old (a carried torch moves; within 1 cm only, its shadow went).
static const float RT_LIGHT_MATCH_DISTANCE = 0.25f;

// The light at `light_position`'s share reaching a render pixel: 0 blocked, 1 reached (or not traced).
float RtLocalLightVisibility(int2 render_pixel, float3 light_position) {
  uint width, height;
  t_rt_light_mask.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  const uint count = min(rt_light_count.x, 128u);
  const uint tile_row = rt_light_count.z;
  const int2 tile = render_pixel >> 3;
  uint nearest = 32u;
  float nearest_distance = RT_LIGHT_MATCH_DISTANCE * RT_LIGHT_MATCH_DISTANCE;
  uint word = 0u;
  [loop]
  for (uint slot = 0u; slot < 32u; ++slot) {
    if (tile_row != 0u && (slot & 3u) == 0u) {
      word = t_rt_light_mask.Load(int3((tile.x * 4) + int((slot >> 2) & 3u), int(tile_row) + (tile.y * 2) + int(slot >> 4), 0));
    }
    const uint light = (tile_row != 0u) ? ((word >> ((slot & 3u) * 8u)) & 0xFFu) : slot;
    if (light >= count) break;
    const float3 offset = rt_light_positions[light].xyz - light_position;
    if (dot(offset, offset) < nearest_distance) {
      nearest_distance = dot(offset, offset);
      nearest = slot;
    }
  }
  if (nearest >= 32u) return 1.f;
  const uint pixel_word = t_rt_light_mask.Load(int3(render_pixel, 0));
  // b9's w 1 (Torch and Fire Shadows from the Light Bounce, bridge RT_LIGHT_RATIO): the word is the pixel's shadow ratio
  // (a float, the same for all its lights), else its bits.
  return (rt_light_count.w != 0u) ? saturate(asfloat(pixel_word)) : (float)((pixel_word >> nearest) & 1u);
}

#endif  // SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
