/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III, static_shadowmap (C883E3FA): a kept shadow map copied into the next one at its new place. The game's own code
// line for line, but the size of a map made larger than the game's largest (shadow_map_size.hpp): its `buffer_size` stays the
// game's 4096 there, so the texel is taken by the map's own size. Any map the game made itself (every Shadows setting) gets the
// game's own `buffer_size`: the result is the game's to the bit (test/tw_shadow_map_size). Made smaller (Shadow Maps Under Rays):
// `buffer_size` x the maps' scale.
#include "../shared.h"

cbuffer static_shadowmap_CB : register(b0) {
  float2 current_tl;
  float2 current_br;
  float2 previous_tl;
  float2 previous_br;
  float2 current_size;
  float2 buffer_size;
  int split_index;
}

Texture2DArray<float4> t_previous_static_shadows : register(t0);

float main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Depth {
  const float2 previous_sum = previous_br + previous_tl;
  const float2 current = uv * (current_br - current_tl) + current_tl;
  const float2 in_previous = -previous_sum * 0.5f + current;
  if (in_previous.x < -1.f || in_previous.y < -1.f || in_previous.x > 1.f || in_previous.y > 1.f) discard;
  uint width;
  uint height;
  uint slices;
  t_previous_static_shadows.GetDimensions(width, height, slices);
  const float scale = shader_injection.shadow_map_scale;
  const float2 map_size = (width > 4096u) ? float2(width, height) : ((scale > 0.f && scale < 1.f) ? buffer_size * scale : buffer_size);
  const float2 texel = floor((in_previous * float2(0.5f, -0.5f) + 0.5f) * map_size);
  return t_previous_static_shadows.Load(int4(int2(texel), split_index, 0)).x;
}
