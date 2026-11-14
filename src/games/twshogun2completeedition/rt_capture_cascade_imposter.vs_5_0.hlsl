/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a sprite soldier's shadow card (E3DA20D4, turned to the shadow camera: the sun) for the outside-view
// geometry shader: the game's corner in world space and the UV of its pixel shader's mask test (459B9CDD's t0), as its vertex
// shader works them out from the same inputs.
#include "./rt_cascade_leaf.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float4x4 view : packoffset(c2);
}

cbuffer imposter_VS : register(b1)
{
  float2 g_imposter_size : packoffset(c0);
}

RtCascadeLeaf main(
  float4 v0 : POSITION0,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : TEXCOORD0)
{
  // The card: standing (v2.w from 0) along the shadow camera's right, or flat on the ground.
  float4 r0, r1;
  r0.xyz = float3(-1, 0, 0) * view._m22_m02_m12;
  r0.xyz = view._m12_m22_m02 * float3(0, 0, -1) + -r0.xyz;
  r1.xyz = r0.xyz * v4.xxx + v0.xyz;
  r1.w = v4.z + r1.y;
  r0.xyz = r0.xyz * v4.yyy + r1.xwz;
  const bool stands = v2.w >= 0.f;
  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.world = float4(stands ? r1.xyz : r0.xyz, 1.f);

  // The game's UV.
  r0.x = 1.f;
  r0.y = -g_imposter_size.y;
  r0.z = -v0.w * g_imposter_size.x + v2.x;
  r1.x = g_imposter_size.x * v2.w;
  r1.x = -r1.x * 8.f + r0.z;
  r0.z = -g_imposter_size.x * 0.99000001f + r1.x;
  r1.z = floor(-r0.z);
  const bool wraps = r0.z < 0.f;
  r1.z = 1.f + r1.z;
  r1.y = v2.y;
  r0.xy = r0.xy * r1.zz + r1.xy;
  r0.xy = wraps ? r0.xy : r1.xy;
  r1.xy = g_imposter_size.xy * v3.xy;
  r0.xy = -r1.xy * float2(0.959999979f, 0.959999979f) + r0.xy;
  r0.z = g_imposter_size.y + r0.y;
  r1.x = v3.x * g_imposter_size.x + v2.x;
  r1.y = -v3.y * g_imposter_size.y + v2.y;
  leaf.uv = stands ? r0.xz : r1.xy;
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  return leaf;
}
