/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a placed variantrigid shadow caster's instances, one record each (its world rows v8-v10), those whose origin is
// in the main view left out (as rt_rows_cascade_rigid).
#include "../twwarhammer3/rt_cascade.hlsli"
#include "./rt_rigid_shadow.hlsli"

void main(
  RT_VARIANT_SHADOW_INPUTS
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out float4 record : TEXCOORD3)
{
  row0 = v8;
  row1 = v9;
  row2 = v10;
  LeaveOutInView(row0, row1, row2);
  record = 0.f;
}
