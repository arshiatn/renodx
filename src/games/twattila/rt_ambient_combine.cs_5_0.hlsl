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
  uint4 size;  // xy the render size; z the rays in place of the game's AO, w lone dark pixels raised to their neighbours (RT_COMBINE_SUN_MAPPING)
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
  float traced = all(id.xy < size.xy) ? ambient[id.xy] : 1.f;
#ifdef RT_COMBINE_SUN_MAPPING
  if (size.w != 0u && all(id.xy < size.xy))
  {
    const int2 last = int2(size.xy) - 1;
    const int2 pixel = int2(id.xy);
    const float neighbours = min(min(ambient[clamp(pixel + int2(-1, 0), 0, last)], ambient[clamp(pixel + int2(1, 0), 0, last)]),
                                 min(ambient[clamp(pixel + int2(0, -1), 0, last)], ambient[clamp(pixel + int2(0, 1), 0, last)]));
    traced = max(traced, neighbours);
  }
  // Inside the traced part only: past it the game's stays.
  if (size.z != 0u && all(id.xy < size.xy)) {
    combined[id.xy] = float4(traced, game.yzw);
    return;
  }
#endif
  combined[id.xy] = float4(game.x * traced, game.yzw);
}
