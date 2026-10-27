/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced fog shadows in the games' fog pass (rt_core.hpp BindFogShadows; the helper's rt_fog traces them at a
// quarter of the render pixels each way): t43 the sun's mean visibility along each camera ray. Nothing bound: the game's
// fog as it was.
#ifndef SRC_GAMES_TWATTILA_RT_FOG_HLSLI_
#define SRC_GAMES_TWATTILA_RT_FOG_HLSLI_

Texture2D<float> t_rt_fog : register(t43);

// What the fog in full shadow keeps of its sun-lit colour (the sky's light it scatters).
static const float RT_FOG_SHADE = 0.35f;

// The factor for the fog's sun-lit colour at a render pixel (bilinear between the fog texels: their centres sit at
// render pixel 4 x + 2).
float RtFogSun(float2 render_pixel) {
  uint width, height;
  t_rt_fog.GetDimensions(width, height);
  if (width == 0u) return 1.f;
  const float2 texel = render_pixel / 4.f - 0.625f;
  const int2 base = int2(floor(texel));
  const float2 blend = texel - base;
  const int2 last = int2(width, height) - 1;
  float corners[4];
  [unroll]
  for (uint corner = 0u; corner < 4u; ++corner) {
    corners[corner] = t_rt_fog.Load(int3(clamp(base + int2(corner & 1u, corner >> 1), 0, last), 0));
  }
  return lerp(RT_FOG_SHADE, 1.f, lerp(lerp(corners[0], corners[1], blend.x), lerp(corners[2], corners[3], blend.x), blend.y));
}

#endif  // SRC_GAMES_TWATTILA_RT_FOG_HLSLI_
