/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: as rt_rows_rigid, for a draw whose pixel shader discards by its instance's fade (faded_pixel_shaders.hpp:
// `floor(TEXCOORD6.w) * 0.001 - t_dither < 0`, TEXCOORD6 the rigid vertex shaders' v9). An instance the game shows on less than half its
// pixels is left out (rt_place: a translation past MAX_TRANSLATION): a mesh drawn at 0 stood solid in the rays over what the picture shows.
// The record holds the fade for the log (rt_core's faded rows line).
#include "./rt_rigid.hlsli"

void main(
  RT_RIGID_INPUTS
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out float4 record : TEXCOORD3) {
  row0 = v6;
  row1 = v7;
  row2 = v8;
  if (floor(v9.w) * 0.001f < 0.5f) {
    row0 = float4(0.f, 0.f, 0.f, 1e30f);
    row1 = row0;
    row2 = row0;
  }
  record = v9;
}
