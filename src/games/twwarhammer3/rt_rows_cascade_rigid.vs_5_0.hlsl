/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a rigid object's instance rows from the shadow pass (6EAC8CE5, 53C5A7EC, 7852254A), out of the view only, one point an instance.
#include "./rt_cascade.hlsli"
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
  LeaveOutInView(row0, row1, row2);
  record = 0.f;
}
