/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a placed rigid shadow caster's instances, one record each (its world rows v6-v8), those whose origin is in the
// main view left out (Warhammer III's rt_cascade.hlsli LeaveOutInView: the main pass has them; cb0[10-13] the last frame's main view,
// Host::cascade_last_view, all 0 for a new mesh's first rows).
#include "../twwarhammer3/rt_cascade.hlsli"
#include "./rt_rigid_shadow.hlsli"

void main(
  RT_RIGID_SHADOW_INPUTS
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out float4 record : TEXCOORD3)
{
  row0 = v6;
  row1 = v7;
  row2 = v8;
  LeaveOutInView(row0, row1, row2);
  record = 0.f;
}
