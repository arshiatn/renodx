/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_SMOKE_MAP_HLSLI_
#define SRC_GAMES_TWATTILA_SMOKE_MAP_HLSLI_

// Smoke shadows without rays (smoke_map.hpp): the settings and the map's view from the sun, its size and its splats.
#define SMOKE_MAP_SIZE 1024.f
#define SMOKE_MAP_COVERAGE 1024.f     // metres across
#define SMOKE_MAP_HEIGHT_SCALE 64.f   // metres: the heights in the moments stay near 1

cbuffer smoke_map_settings : register(b2)
{
  float g_smoke_sun_direction;  // its first float in lighting_VS_PS
  float g_smoke_strength;
  float g_smoke_softness;       // metres
  uint g_smoke_particles;       // streamed out last frame
  float2 g_smoke_render_size;   // the 3D image's (DLSS: the depth's top-left corner)
}

struct SmokeView
{
  float3 right;    // across the sun's rays
  float coverage;  // metres across the map (0: none, the sun too low)
  float3 up;
  float padding0;
  float2 origin;   // the map's centre along right and up, on whole texels
  float2 padding1;
  float height;    // the height its moments are relative to (only their precision depends on it)
  float3 padding2;
};

struct SmokeSplat
{
  float2 centre;    // in the map's clip space
  float radius;     // in it too
  float padding;
  float4 optical;   // at its middle: optical depth, x height, x (height^2 + extent^2 / 3), 0 (heights / SMOKE_MAP_HEIGHT_SCALE)
};

#endif  // SRC_GAMES_TWATTILA_SMOKE_MAP_HLSLI_
