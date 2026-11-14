/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: grass in the shadow pass (rigidgrass / rigidgrasswet vs30_shadow, F6CBD06E: drawn with the in-game grass
// shadows on) for the outside-view geometry shader: the game's corner in world space, its shrink with the distance and its sway
// worked out as its vertex shader does (from the pass's own camera), and the UV of its pixel shader's mask test (4DAAA691: the
// diffuse map's alpha at t0 x its dissolve map, under 0.25 discarded; the dissolve left out, as the leaves' fade).
#include "./rt_cascade_leaf.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 inv_view : packoffset(c18);
  float time_in_sec : packoffset(c35);
}

cbuffer wind_VS : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
  float wind_vegetation_multiplier : packoffset(c1.z);
  float wind_vegetation_hstrength : packoffset(c1.w);
  float wind_vegetation_vstrength : packoffset(c2);
  float g_wind_influence_max_distance : packoffset(c2.y);
  float g_wind_influence_offset : packoffset(c2.z);
}

cbuffer grass_PS : register(b2)
{
  float inv_lod_distance : packoffset(c0);
}

RtCascadeLeaf main(float4 v0 : POSITION0, float2 v1 : TEXCOORD0, float4 v4 : COLOR0, float4 v5 : TEXCOORD1, float4 v6 : TEXCOORD2,
                   float4 v7 : TEXCOORD3)
{
  // The blade sinks toward -0.6 along the camera's up with the clump's (v5-v7.w) distance, by COLOR0.w.
  const float shrink = 1.f - (saturate(v4.w) * saturate(inv_lod_distance * distance(float3(v5.w, v6.w, v7.w), camera_position)));
  const float lowered = (shrink * (0.6f + v0.y)) - 0.6f;
  const float3 placed = float3(dot(v5, v0), dot(v6, v0), dot(v7, v0)) + float3(inv_view._m10 * lowered, 0.f, inv_view._m12 * lowered);
  // A bob up and a sway along the wind (the lower blades' share by COLOR0.w), fading out over the wind's influence distance.
  const float2 waves = sin(float2((placed.z * 0.25f) + time_in_sec, 0.3f * ((placed.z * 0.25f) + time_in_sec)));
  const float2 shares = saturate(float2(0.3f, 0.65f) - v4.ww) * wind_vegetation_multiplier;
  const float bob = shares.x * (wind_vegetation_vstrength * waves.y) * cos(5.f * (time_in_sec + placed.x));
  const float gust = wind_vegetation_hstrength * ((wind_vegetation_hstrength * (waves.x * sin(3.f * time_in_sec)) * 0.5f) + 0.5f);
  const float2 along = 0.5f * ((g_wind_direction * shares.y) * gust);
  const float near = min(g_wind_influence_offset, g_wind_influence_max_distance - 1.f);
  const float faded = saturate((distance(placed, camera_position) - near) / (g_wind_influence_max_distance - near));

  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = v1;
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = float4(placed + ((1.f - faded) * float3(along.x, 2.33333325f * bob, along.y)), 1.f);
  return leaf;
}
