/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's swaying placed instances left still (Swaying Shadows Range, Off-Screen Sway: Experimental,
// dlaa_helper/rt_d3d12.hpp): rt_compact.cs_6_5.hlsl leaves out the swaying copy of an instance SwayTraced turns down,
// rt_place.cs_6_5.hlsl then shows its placed (still) copy instead of hiding it. Both ask with the instance's rows (its
// origin) and the same constants: no instance goes missing. Grass Ray Tracing Range (Experimental): both leave a grass
// instance past it out of the scene, swaying and still copies alike (GrassInRange).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SWAY_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SWAY_HLSLI_

static const uint SWAY_CUT = 1u;      // swaying copies left out past the range or the view
static const uint SWAY_IN_VIEW = 2u;  // swaying copies only near the view
static const float SWAY_VIEW_MARGIN = 16.f;  // metres outside the view's sides

// False: `world` is outside the view's four sides (from the view-projection's rows 0, 1 and 3) moved out by `margin`
// metres.
bool NearView(float4 world, float4 row0, float4 row1, float4 row3, float margin) {
  const float4 sides[4] = {row3 + row0, row3 - row0, row3 + row1, row3 - row1};
  [unroll]
  for (uint side = 0u; side < 4u; ++side) {
    if (dot(sides[side], world) < -margin * length(sides[side].xyz)) return false;
  }
  return true;
}

// True: a swaying instance at `origin` keeps its swaying copy: within `range` metres of the camera along the view (0:
// any) and, with SWAY_IN_VIEW, near the view.
bool SwayTraced(float3 origin, float4 row0, float4 row1, float4 row3, float range, uint flags) {
  const float4 world = float4(origin, 1.f);
  if (range > 0.f && abs(dot(row3, world)) > range) return false;
  return (flags & SWAY_IN_VIEW) == 0u || NearView(world, row0, row1, row3, SWAY_VIEW_MARGIN);
}

// True: a grass instance at `origin` stays in the scene: within `range` metres of the camera along the view (the
// view-projection's row 3; 0: any). Its shadows farther off are below a pixel.
bool GrassInRange(float3 origin, float4 row3, float range) {
  return range <= 0.f || abs(dot(row3, float4(origin, 1.f))) <= range;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SWAY_HLSLI_
