/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): Warhammer's and Warhammer II's G-buffer and camera for the passes
// (twattila/light_bounce_prepare.hlsli, light_bounce_trace.hlsli, light_bounce_apply.hlsli), as their deferred sun
// light (directionallight.hlsl) reads them; twattila/light_bounce.hpp binds that draw's channels 1-4 at t42-t45 and its
// camera at b11 (the last frame's at b10).
cbuffer camera : register(b11) {
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
  float2 g_viewport_origin : packoffset(c30);
}

// The last frame's camera (light_bounce.hpp copies it after each trace): what BouncePrevious reads.
cbuffer previous_camera : register(b10) {
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c9);
}

#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer gbuffer_normal : register(t42);     // channel 1: world normal (spherical angles in xy)
BounceGBuffer gbuffer_colour : register(t43);     // channel 2: albedo
BounceGBuffer gbuffer_constants : register(t44);  // channel 3: specular colour
BounceGBuffer gbuffer_depth : register(t45);      // channel 4

float BounceDepth(int2 texel) {
  return BounceLoad(gbuffer_depth, texel).x;
}

// At a depth buffer value `depth` (the trace rebuilds the prepared geometry's positions from their depth).
float3 BounceViewPositionAt(int2 texel, float depth) {
  const float2 uv = (float2(texel) + 0.5f - g_viewport_origin + g_vpos_texel_offset) * g_viewport_dimensions.zw;
  const float4 ndc = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f);
  const float4 position = mul(ndc, inv_projection);
  return position.xyz / position.w;
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

// Skin keeps the same angles in the upper bits of its 10-bit channels (8 bits each): close enough here.
float3 BounceViewNormal(int2 texel) {
  const float2 angles = BounceLoad(gbuffer_normal, texel).xy * float2(3.14159265f, 6.2831853f)
                        - float2(1.57079633f, 3.14159265f);
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  const float3 world = float3(cosines.y * cosines.x, sines.x, sines.y * cosines.x);
  return normalize(mul(world, (float3x3)view));
}

// As the game's ambient: albedo x (1 - specular colour).
float3 BounceDiffuseAlbedo(int2 texel) {
  const float3 specular = BounceLoad(gbuffer_constants, texel).rgb;
  return BounceLoad(gbuffer_colour, texel).rgb * saturate(1.f - specular);
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
  return float3(uv * g_viewport_dimensions.xy + g_viewport_origin - g_vpos_texel_offset,
                length(world - previous_camera_position));
}
