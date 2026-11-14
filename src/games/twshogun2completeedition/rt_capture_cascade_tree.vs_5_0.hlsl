/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a tree's leaves or branches as the shadow pass draws them (rigidleafwet_v3 5851CA62, rigidbranchwet_v3
// 6F20485E: one vertex shader's math) for the outside-view geometry shader: the corner in world space, its instance's two row
// sets blended and swayed by the wind as the game's, and the UV of the leaves' mask test (A97EF2E0's t1).
#include "./rt_cascade_leaf.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float time_in_sec : packoffset(c35);
}

cbuffer wind_VS : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
  float wind_vegetation_multiplier : packoffset(c1.z);
  float wind_vegetation_hstrength : packoffset(c1.w);
  float wind_vegetation_vstrength : packoffset(c2);
}

RtCascadeLeaf main(
  float4 v0 : POSITION0,
  float2 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : TEXCOORD0,
  float4 v8 : TEXCOORD1,
  float4 v9 : TEXCOORD2,
  float4 v10 : TEXCOORD3,
  float4 v11 : TEXCOORD4,
  float4 v12 : TEXCOORD5,
  float4 v13 : TEXCOORD6)
{
  // The two row sets, blended by v5.xy at the sets v5.zw names.
  const float3 sets[2] = {float3(dot(v7, v0), dot(v8, v0), dot(v9, v0)), float3(dot(v11, v0), dot(v12, v0), dot(v13, v0))};
  const uint2 picked = (uint2)v5.zw;
  float4 r0 = (float4(sets[picked.x], v0.w) * v5.x) + (float4(sets[picked.y], v0.w) * v5.y);

  // The game's wind.
  float4 r1, r2;
  r1.x = v10.x * 4 + time_in_sec;
  r1.yz = float2(3, 0.5) * time_in_sec;
  r1.xyz = sin(r1.xyz);
  r1.x = r1.x * r1.y;
  r1.y = r1.z * 0.5 + 0.75;
  r1.x = wind_vegetation_hstrength * r1.x;
  r1.x = r1.x * 0.5 + 1;
  r1.x = r1.x * r1.y;
  r1.x = -wind_vegetation_hstrength * r1.x;
  r1.y = wind_vegetation_multiplier * v6.w;
  r1.y = v6.w * r1.y;
  r1.yz = g_wind_direction.xy * r1.yy;
  r1.xz = r1.yz * r1.xx;
  r1.y = 0;
  r1.xyz = r1.xyz + r0.xyz;
  r1.w = time_in_sec + r0.x;
  r1.w = 6 * r1.w;
  r1.w = sin(r1.w);
  r2.x = v10.x * 2 + time_in_sec;
  r2.x = 0.300000012 * r2.x;
  r2.x = sin(r2.x);
  r2.x = wind_vegetation_vstrength * r2.x;
  r1.w = r2.x * r1.w;
  r2.x = saturate(-0.699999988 + v6.w);
  r2.x = wind_vegetation_multiplier * r2.x;
  r1.w = r2.x * r1.w;
  r0.xyz = r1.www * float3(0, 3.33333325, 0) + r1.xyz;

  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = v4;
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = float4(r0.xyz / r0.w, 1.f);
  return leaf;
}
