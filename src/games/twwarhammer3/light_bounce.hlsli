/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): Warhammer III's G-buffer and camera for the passes (twattila/light_bounce_prepare.hlsli,
// light_bounce_trace.hlsli, light_bounce_apply.hlsli), as its deferred sun light (directionallight.hlsl) reads them;
// twattila/light_bounce.hpp binds that draw's channels 1-4 at t42-t45 and its camera at b11 (the last frame's at b10).
#include "../twattila/light_bounce.h"

cbuffer camera : register(b11) {
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
}

// The last frame's camera (light_bounce.hpp copies it after each trace): what BouncePrevious reads.
cbuffer previous_camera : register(b10) {
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c10);
}

#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer gbuffer_normal : register(t42);    // channel 1: view-space normal in xy
BounceGBuffer gbuffer_colour : register(t43);    // channel 2: albedo; LB.diffuse_rule 1, its a: the F0's scale
BounceGBuffer gbuffer_material : register(t44);  // channel 3: x metalness, with LB.diffuse_rule above 0
BounceGBuffer gbuffer_depth : register(t45);     // channel 4

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

// As the game: a unit vector rebuilt around the view axis, from xy (LB.diffuse_rule 0: the upper 8 of each 10 bits).
float3 BounceViewNormal(int2 texel) {
  const float2 stored = BounceLoad(gbuffer_normal, texel).xy;
  const float2 e = (LB.diffuse_rule > 0.f) ? (stored * 2.f - 1.f) : (float2(uint2(round(stored * 1023.f)) >> 2) * (2.f / 255.f) - 1.f);
  const float rest = 1.f - dot(e, e);
  return normalize(float3(2.f * e * sqrt(max(rest, 0.f)), 1.f - 2.f * rest));
}

// As each of the game's sun shaders takes its ambient (LB.diffuse_rule 0: albedo x 0.96, no metal; else albedo x (1 - metal)
// x (1 - F0), F0 from 0.08 x channel 2's a (1) or 0.04 (2) on dielectrics to the albedo on metal).
float3 BounceDiffuseAlbedo(int2 texel) {
  const float4 colour = BounceLoad(gbuffer_colour, texel);
  if (LB.diffuse_rule == 0.f) return colour.rgb * 0.96f;
  const float metal = BounceLoad(gbuffer_material, texel).x;
  const float dielectric = (LB.diffuse_rule == 1.f) ? colour.a * 0.08f : 0.04f;
  return colour.rgb * (1.f - metal) * (1.f - lerp(dielectric.xxx, colour.rgb, metal));
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
