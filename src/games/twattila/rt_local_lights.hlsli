/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced local light shadows in the games' light passes (rt_core.hpp LocalLightMask; the helper's rt_trace traces
// them at the render pixels): t42 the lit bits of this frame's traced lights, b12 their world positions (the bits'
// order) and count. A light not among them, or nothing bound, stays as the game draws it.
#ifndef SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
#define SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_

Texture2D<uint> t_rt_light_mask : register(t42);
cbuffer rt_local_lights : register(b12) {
  float4 rt_light_positions[32];
  uint4 rt_light_count;  // x
};

// The light at `light_position`'s share reaching a render pixel: 0 blocked, 1 reached (or not traced).
float RtLocalLightVisibility(int2 render_pixel, float3 light_position) {
  uint width, height;
  t_rt_light_mask.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  [loop]
  for (uint light = 0u; light < min(rt_light_count.x, 32u); ++light) {
    if (all(abs(rt_light_positions[light].xyz - light_position) < 0.01f)) {
      return (float)((t_rt_light_mask.Load(int3(render_pixel, 0)) >> light) & 1u);
    }
  }
  return 1.f;
}

#endif  // SRC_GAMES_TWATTILA_RT_LOCAL_LIGHTS_HLSLI_
