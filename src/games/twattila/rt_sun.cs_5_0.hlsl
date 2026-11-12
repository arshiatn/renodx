/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Total War ray tracing (rt_scene.hpp): at the frame's first sun light draw, the direction toward the sun (the light's
// sun_direction points from it, as the screen-space shadows read it) into the sun texture's texel 0 for the next trace.
// The draw's lighting buffer at b0; where its sun_direction starts (in floats: Attila's c0.y is 1) at b1. With the
// ambient cube's place (v18: Attila's c2, the game's own ambient, six colours: +x, -x, +y, -y, +z, -z) its colours
// into texels 3-8 (w 1; texel 3's w 0: none), the paths' sky (dlaa_helper/rt_path.hlsli SkyLight). Attila's sun light
// shaders scale that ambient by c11.x's choice: 360 x 0.004 (c11.x > 0) or 1, their foliage ones 0.6 or 0.8 of that:
// texel 4's w the first (the paths' sky in the game's units), texel 5's w the foliage's share (light_bounce_apply).
// TW_RT_SUN_SCALE_SWITCH 0 (an includer's define): a game without that choice (Pharaoh: its float there is
// g_cloud_shadow_position.x, the cube taken x 1): the unscaled weights always.
#ifndef TW_RT_SUN_SCALE_SWITCH
#define TW_RT_SUN_SCALE_SWITCH 1
#endif
cbuffer lighting_VS_PS : register(b0)
{
  float4 lighting[16];
}

cbuffer rt_sun : register(b1)
{
  uint4 rt_sun;  // x the first float of sun_direction, y of the ambient cube (0: none)
}

RWTexture2D<float4> sun : register(u0);

float LightingValue(uint index)
{
  return lighting[index / 4u][index % 4u];
}

[numthreads(1, 1, 1)]
void main()
{
  const float3 sun_direction = float3(LightingValue(rt_sun.x), LightingValue(rt_sun.x + 1u), LightingValue(rt_sun.x + 2u));
  const float length_squared = dot(sun_direction, sun_direction);
  sun[uint2(0, 0)] = (length_squared > 0.f) ? float4(-sun_direction * rsqrt(length_squared), 1.f) : 0.f;
  // c11.x: nine registers past the cube's c2.
  const bool scaled = TW_RT_SUN_SCALE_SWITCH && rt_sun.y != 0u && LightingValue(rt_sun.y + 36u) > 0.f;
  [unroll]
  for (uint side = 0u; side < 6u; ++side)
  {
    const uint first = rt_sun.y + side * 4u;
    const float w = (side == 1u) ? (scaled ? 1.44f : 1.f) : ((side == 2u) ? (scaled ? 0.6f : 0.8f) : 1.f);
    sun[uint2(3u + side, 0)] = (rt_sun.y != 0u) ? float4(LightingValue(first), LightingValue(first + 1u), LightingValue(first + 2u), w) : 0.f;
  }
}
