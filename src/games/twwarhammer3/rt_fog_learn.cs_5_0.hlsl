/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's Fog in Reflections (addon.cpp reflections::OnFogLight): after the fog's light draw (A8F5202D, half the
// render's size) the game's fog numbers (its own buffer, bound here as it was there) into the sun texture's texels 9-13 for
// the helper's reflections (twattila/rt_leg_fog.hlsli), with the light its fog shows where it hides everything: over a grid
// of the picture, the fog's light the game just drew over what its opacity hides there (the same formula the reflections
// take, from the camera). The next frame's reflections read them.
#include "../twattila/rt_leg_fog.hlsli"

cbuffer fog : register(b0)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_lerp : packoffset(c4);
  float g_fog_clear_distance : packoffset(c4.y);
  float g_campaign_density_scaling : packoffset(c4.w);
  float g_height_fog_clear_distance : packoffset(c5);
}

cbuffer camera : register(b1)
{
  float3 camera_position : packoffset(c0);
}

cbuffer rt_fog_learn : register(b2)
{
  float fog_strength;  // the addon's Fog Strength, as battlefog00 and battlefog01 take it
  uint2 render_size;
}

Texture2D<float4> fog_light : register(t0);
Texture2D<float4> positions : register(t1);  // world positions (w 0: the sky)
RWTexture2D<float4> sun : register(u0);

static const uint2 GRID = uint2(32u, 18u);

[numthreads(1, 1, 1)]
void main()
{
  // 34719E09's literals: the air's scattering by colour and its two heights, the fog densities' scale.
  float4 texels[5] = {
      float4(0.f, 0.f, 0.f, 1.f),
      float4(g_fog_height_top, g_fog_height_bottom, g_fog_density_height, g_fog_density_constant),
      float4(g_fog_clear_distance, g_height_fog_clear_distance, g_campaign_density_scaling, g_fog_lerp),
      float4(5.8e-6f, 1.35e-5f, 3.31e-5f, 2.2e-5f),
      float4(0.000206276105f, 0.00120224594f, 0.0125f, fog_strength),
  };
  const LegFog fog = LegFogOf(texels);
  uint light_width, light_height;
  fog_light.GetDimensions(light_width, light_height);
  float3 added = 0.f;
  float3 hidden = 0.f;
  [loop]
  for (uint y = 0u; y < GRID.y; ++y)
  {
    [loop]
    for (uint x = 0u; x < GRID.x; ++x)
    {
      const uint2 pixel = (uint2(x, y) * 2u + 1u) * render_size / (GRID * 2u);
      const float4 position = positions[pixel];
      if (position.w <= 0.f) continue;
      added += max(fog_light[min(pixel / 2u, uint2(light_width, light_height) - 1u)].rgb, 0.f) * fog_strength;
      hidden += 1.f - LegFogLeft(fog, camera_position, position.xyz, 0.f);
    }
  }
  // Less hidden than one grid point's all (a clear day): the little light there is, not that over almost nothing.
  texels[0].rgb = added / max(hidden, 1.f);
  [unroll]
  for (uint texel = 0u; texel < 5u; ++texel)
  {
    sun[uint2(LEG_FOG_TEXEL + texel, 0)] = texels[texel];
  }
}
