/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's campaign plant rows (rt_scene.hpp): the placement of the game's rigidcampaignvegetation (0x84797031) for ray
// tracing, one point per instance through stream output: its size and turn around the vertical, standing
// (rt_capture_campaign_vegetation).

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : COLOR0,
  float4 v3 : POSITION1,
  float v4 : POSITION2,
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out uint4 record : TEXCOORD3)
{
  float turn_sin, turn_cos;
  sincos(v3.w, turn_sin, turn_cos);
  row0 = float4(v4 * turn_cos, 0.f, v4 * turn_sin, v3.x);
  row1 = float4(0.f, v4, 0.f, v3.y);
  row2 = float4(-v4 * turn_sin, 0.f, v4 * turn_cos, v3.z);
  record = 0u;
}
