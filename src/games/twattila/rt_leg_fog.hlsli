/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The game's fog along one leg of a light path that does not start at the camera (a reflection's ray from the water to the
// hill it meets): what the helper's reflections put between a pixel and what its ray met (dlaa_helper/rt_path.hlsli), and what
// the addon measures the fog's light against (twwarhammer3/rt_fog_learn.cs_5_0.hlsl). The numbers are the game's own, in the
// sun texture's texels 9-13 (texel 9's w 0: none), so the helper holds no game's constants.
// The opacity is Warhammer III's fx_battlefog (34719E09) with the leg's start in the camera's place and its clear distances
// shortened by what the light had travelled before the leg: the height fog's density (1 under `height_bottom`, falling to 0 at
// `height_top`) summed along the leg's first (length - height clear distance) metres, or the constant fog past its clear
// distance, whichever hides more; plus the air's own (`rayleigh`, `mie`: by the end's height) past the nearer clear distance.
// Left out: the game's noise on the direction's height (0.04 at most) and its legacy forced fog.
#ifndef SRC_GAMES_TWATTILA_RT_LEG_FOG_HLSLI_
#define SRC_GAMES_TWATTILA_RT_LEG_FOG_HLSLI_

static const uint LEG_FOG_TEXEL = 9u;  // the sun texture's first texel of five

struct LegFog {
  float3 source;  // the light the fog shows where it hides everything (learned from the game's fog light)
  float set;      // 0: no fog known
  float height_top;
  float height_bottom;
  float density_height;
  float density_constant;
  float clear_distance;
  float height_clear_distance;
  float distance_scale;
  float amount;  // the game's blend toward its fog (g_fog_lerp)
  float3 rayleigh;
  float mie;
  float rayleigh_fall;  // per metre of height, as a power of two
  float mie_fall;
  float density_scale;
  float strength;  // the addon's Fog Strength
};

LegFog LegFogOf(float4 texels[5]) {
  LegFog fog;
  fog.source = texels[0].rgb;
  fog.set = texels[0].w;
  fog.height_top = texels[1].x;
  fog.height_bottom = texels[1].y;
  fog.density_height = texels[1].z;
  fog.density_constant = texels[1].w;
  fog.clear_distance = texels[2].x;
  fog.height_clear_distance = texels[2].y;
  fog.distance_scale = texels[2].z;
  fog.amount = texels[2].w;
  fog.rayleigh = texels[3].rgb;
  fog.mie = texels[3].w;
  fog.rayleigh_fall = texels[4].x;
  fog.mie_fall = texels[4].y;
  fog.density_scale = texels[4].z;
  fog.strength = texels[4].w;
  return fog;
}

// The share of light the fog leaves from `end` to `start`, by colour; `travelled`: metres from the camera to `start`.
float3 LegFogLeft(LegFog fog, float3 start, float3 end, float travelled) {
  const float3 to_end = end - start;
  const float leg = length(to_end);
  const float height_clear = max(fog.height_clear_distance - travelled, 0.f);
  const float clear = max(fog.clear_distance - travelled, 0.f);
  const float near_clear = min(height_clear, clear);
  float3 left = 1.f;
  if (leg > near_clear + 1e-4f) {
    const float slope = to_end.y / leg;
    const float fall = (abs(slope) < 1e-6f) ? -1e-6f : -slope;
    const float at_top = (start.y - fog.height_top) / fall;
    const float at_bottom = (start.y - fog.height_bottom) / fall;
    const float at_floor = (start.y + 10000.f) / fall;
    const float reach = max(leg - height_clear, 0.f);
    const float ramp_from = max(min(at_top, at_bottom), 0.f);
    const float ramp_to = min(max(reach, ramp_from), max(max(at_top, at_bottom), 0.f));
    const float full_from = max(min(at_floor, at_bottom), 0.f);
    const float full_to = min(max(reach, full_from), max(max(at_floor, at_bottom), 0.f));
    float thickness = 0.f;
    if (ramp_to != ramp_from) {
      const float heights = (start.y - ramp_from * fall) + (start.y - ramp_to * fall) - 2.f * fog.height_top;
      thickness = (ramp_from - ramp_to) * heights / (2.f * max(fog.height_top - fog.height_bottom, 1e-3f));
    }
    if (full_to != full_from) {
      thickness += full_to - full_from;
    }
    const float thick = max(fog.density_height * thickness, fog.density_constant * max(leg - clear, 0.f));
    const float height = max(end.y, 0.f);
    const float3 air = (fog.rayleigh * exp2(-fog.rayleigh_fall * height) + fog.mie * exp2(-fog.mie_fall * height))
                       * (fog.distance_scale * (leg - near_clear));
    left = exp(-(thick * fog.density_scale + air));
  }
  return 1.f + fog.amount * max(fog.strength * (left - 1.f), -1.f);
}

#endif  // SRC_GAMES_TWATTILA_RT_LEG_FOG_HLSLI_
