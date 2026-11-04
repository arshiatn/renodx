/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a placed rigid draw's instance rows, one point an instance, for the stream output.
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
  record = 0.f;
}
