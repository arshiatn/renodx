/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced local light shadows in the games' light passes (rt_core.hpp BindLocalLights; the helper's rt_trace traces
// them at the render pixels): t42 the lit bits of this frame's traced lights, b9 their world positions (the bits'
// order) and count (b12 is the render scale's: render_scale.hlsli). A light not among them, or nothing bound, stays as
// the game draws it.
#ifndef SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
#define SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_

Texture2D<uint> t_rt_light_mask : register(t42);
cbuffer rt_local_lights : register(b9) {
  float4 rt_light_positions[32];  // xyz, w the reach (metres)
  uint4 rt_light_count;           // x; y the glow (asfloat: torch and fire light in fog and smoke, 0 none)
  float4 rt_light_colours[32];    // rgb
};

// The local lights' light reaching a world point (no shadows): each one's colour times the games' smooth falloff
// (1 - distance / reach)^2, times the glow setting. Torches and fires lighting the smoke and dust near them (vfx00).
float3 RtLocalLightAt(float3 world) {
  float3 light = 0.f;
  [loop]
  for (uint index = 0u; index < min(rt_light_count.x, 32u); ++index) {
    const float falloff = saturate(1.f - (distance(rt_light_positions[index].xyz, world) / max(rt_light_positions[index].w, 1e-3f)));
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
  uint nearest = 32u;
  float nearest_distance = RT_LIGHT_MATCH_DISTANCE * RT_LIGHT_MATCH_DISTANCE;
  [loop]
  for (uint light = 0u; light < min(rt_light_count.x, 32u); ++light) {
    const float3 offset = rt_light_positions[light].xyz - light_position;
    if (dot(offset, offset) < nearest_distance) {
      nearest_distance = dot(offset, offset);
      nearest = light;
    }
  }
  return (nearest < 32u) ? (float)((t_rt_light_mask.Load(int3(render_pixel, 0)) >> nearest) & 1u) : 1.f;
}

#endif  // SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
