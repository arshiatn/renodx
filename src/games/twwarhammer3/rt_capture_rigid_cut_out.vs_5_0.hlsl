/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: as rt_capture_rigid, with the UV the game's pixel shaders clip by (their TEXCOORD2, this input) in w.
#include "../twattila/rt_capture.hlsli"
#include "./rt_rigid.hlsli"

void main(
  RT_RIGID_INPUTS
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0) {
  capture_world = RtWorldUv(RtRigidWorld(v0, v6, v7, v8), v1.xy);
  capture_position = 0.f;
}
