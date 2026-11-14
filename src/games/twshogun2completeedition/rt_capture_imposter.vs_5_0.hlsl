/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2's sprite soldiers (imposter vs30_main, 0x03FB2979) for ray tracing's stream output (twattila/rt_core.hpp,
// Host::scene_draw's capture version: in clip space with the draw's camera, as the game's SV_Position, its UV at
// TEXCOORD1), as Attila's rt_capture_imposter: the card turned to the sun instead of the camera, so its shadow keeps its
// width and does not swing with the view. The game's UV as it writes it (its vertex shader, from the same inputs).
// t0: rt_sun, its first texel the direction toward the sun.
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
}

cbuffer imposter_VS : register(b1)
{
  float2 g_imposter_size : packoffset(c0);
}

Texture2D<float4> t_rt_sun : register(t0);

void main(
  float4 v0 : POSITION0,
  float3 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : TEXCOORD0,
  out float4 position : SV_Position0,
  out float2 uv : TEXCOORD1)
{
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float3 right = (across > 1e-3f) ? float3(-toward_sun.z, 0.f, toward_sun.x) / across : float3(1.f, 0.f, 0.f);

  // The game's card (standing, or flat on the ground with v2.w below 0), its right vector replaced.
  float3 standing = right * v3.z + v0.xyz;
  standing.y += v4.w;
  const float3 flat_card = right * v4.y + float3(standing.x, standing.y - v4.z, standing.z);
  const bool stands = v2.w >= 0.f;
  position = mul(float4(stands ? standing : flat_card, 1.f), view_projection);

  // The game's UV.
  float4 r0, r1;
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
  r0.xy = -r1.xy * float2(0.980000019f, 0.99000001f) + r0.xy;
  r0.y = v4.z + r0.y;
  r0.z = g_imposter_size.y + r0.y;
  const float2 flat_uv = float2(r1.x * 0.980000019f + v2.x, -r1.y * 0.99000001f + v2.y);
  uv = stands ? r0.xz : flat_uv;
}
