/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a tree's instance rows from the shadow pass, out of the view only, one point an instance.
#include "./rt_cascade.hlsli"

void main(
  RT_TREE_INPUTS
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out float4 record : TEXCOORD3) {
  row0 = v8;
  row1 = v9;
  row2 = v10;
  LeaveOutInView(row0, row1, row2);
  record = 0.f;
}
