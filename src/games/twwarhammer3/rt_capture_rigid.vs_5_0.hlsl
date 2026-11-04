/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: a placed rigid mesh's first instance in world space for the stream output.
#include "./rt_rigid.hlsli"

void main(
  RT_RIGID_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0) {
  capture_world = float4(RtRigidWorld(v0, v6, v7, v8), 1.f);
  capture_position = 0.f;
}
