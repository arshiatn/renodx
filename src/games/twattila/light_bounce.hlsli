/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): Attila's, Thrones of Britannia's and Rome II's G-buffer and camera for the trace and the
// apply (light_bounce_prepare.hlsli, light_bounce_trace.hlsli, light_bounce_apply.hlsli), as their deferred sun light
// (directionallight.hlsl) reads them; light_bounce.hpp binds that draw's channels 1-4 at t42-t45 and its camera at
// b11 (the last frame's at b10).
cbuffer camera_VS_PS : register(b11) {
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
}

// The last frame's camera_VS_PS (light_bounce.hpp copies it after each trace): what BouncePrevious reads.
cbuffer previous_camera : register(b10) {
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c9);
}

#include "./light_bounce_gbuffer.hlsli"

BounceGBuffer gbuffer_normal : register(t42);  // channel 1: world normal x 0.5 + 0.5, gloss
BounceGBuffer gbuffer_colour : register(t43);  // channel 2: albedo (gamma 2), metalness
BounceGBuffer gbuffer_depth : register(t45);   // channel 4

float BounceDepth(int2 texel) {
  return BounceLoad(gbuffer_depth, texel).x;
}

// At a depth buffer value `depth` (the trace rebuilds the prepared geometry's positions from their depth).
float3 BounceViewPositionAt(int2 texel, float depth) {
  const float2 uv = (float2(texel) + 0.5f + g_vpos_texel_offset) * g_viewport_dimensions.zw;
  const float4 ndc = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f);
  const float4 position = float4(dot(ndc, inv_projection._m00_m10_m20_m30), dot(ndc, inv_projection._m01_m11_m21_m31),
                                 dot(ndc, inv_projection._m02_m12_m22_m32), dot(ndc, inv_projection._m03_m13_m23_m33));
  return position.xyz / position.w;
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

float3 BounceViewNormal(int2 texel) {
  const float3 world = normalize(BounceLoad(gbuffer_normal, texel).xyz * 2.f - 1.f);
  return normalize(float3(dot(world, view._m00_m10_m20), dot(world, view._m01_m11_m21), dot(world, view._m02_m12_m22)));
}

// As the game's ambient: albedo (gamma 2) x (1 - metalness).
float3 BounceDiffuseAlbedo(int2 texel) {
  const float4 colour = BounceLoad(gbuffer_colour, texel);
  return colour.rgb * colour.rgb * (1.f - colour.a);
}

float BouncePixelsPerMetre(float distance) {
  return 0.5f * g_viewport_dimensions.y * projection._m11 / distance;
}

float2 BounceViewportSize() {
  return g_viewport_dimensions.xy;
}

float3 BouncePrevious(float3 view_position) {
  const float3 world = mul(float4(view_position, 1.f), inv_view).xyz;
  const float4 clip = mul(float4(world, 1.f), previous_view_projection);
  // Behind the last camera (or none yet): off screen.
  if (!(clip.w > 0.f)) return float3(-1e9f, -1e9f, 0.f);
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  return float3(uv * g_viewport_dimensions.xy - g_vpos_texel_offset, length(world - previous_camera_position));
}
