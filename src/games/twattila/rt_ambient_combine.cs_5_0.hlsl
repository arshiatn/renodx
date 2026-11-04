/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Total War ray tracing (rt_scene.hpp), for the games without an ambient occlusion pass of the addon's (Warhammer): the
// AO their sun draws read (t0, any size: sampled where it holds the pixel; its AO in red, as ao_filter.hlsli reads it)
// times the traced ambient visibility (t1, at the render size's pixel), the game's other channels kept, at the display's
// size for the sun draws in its place.
Texture2D<float4> game_ao : register(t0);
Texture2D<float> ambient : register(t1);
SamplerState linear_clamp : register(s0);
RWTexture2D<float4> combined : register(u0);

cbuffer rt_combine : register(b0)
{
  uint4 size;  // xy the render size
}

#ifdef RT_COMBINE_SUN_MAPPING
// The sun draw's own (Warhammer III): below full size the AO has its own overscan.
cbuffer cb_ssao : register(b1)
{
  float warscape_3d_overscan;
  float ssao_overscan;
}
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 target;
  combined.GetDimensions(target.x, target.y);
  if (any(id.xy >= target)) return;
  float2 uv = (float2(id.xy) + 0.5f) / float2(target);
#ifdef RT_COMBINE_SUN_MAPPING
  uv *= ssao_overscan / warscape_3d_overscan;
#endif
  const float4 game = game_ao.SampleLevel(linear_clamp, uv, 0);
  const float traced = all(id.xy < size.xy) ? ambient[id.xy] : 1.f;
  combined[id.xy] = float4(game.x * traced, game.yzw);
}
