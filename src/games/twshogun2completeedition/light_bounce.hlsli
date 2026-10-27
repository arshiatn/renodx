/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): Shogun 2's G-buffer and camera for the trace and the apply
// (twattila/light_bounce_prepare.hlsli, light_bounce_trace.hlsli, light_bounce_apply.hlsli), as its deferred sun
// light (sunlight00-03) reads them; twattila/light_bounce.hpp binds that draw's G-buffer (t7-t10) at t42-t45 and its
// camera (b0) at b11 (the last frame's at b10).
cbuffer camera_VS_PS : register(b11) {
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);  // the render size with the DLSS addon's render scale
  float2 g_viewport_origin : packoffset(c39);
}

// The last frame's camera_VS_PS (light_bounce.hpp copies it after each trace): what BouncePrevious reads.
cbuffer previous_camera : register(b10) {
  float3 previous_camera_position : packoffset(c0);
  float4x4 previous_view_projection : packoffset(c10);
}

#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer gbuffer_normal : register(t42);     // world normal (spherical angles in xy)
BounceGBuffer gbuffer_colour : register(t43);     // albedo
BounceGBuffer gbuffer_constants : register(t44);  // metalness (x), flags (w x 255)
BounceGBuffer gbuffer_depth : register(t45);

float BounceDepth(int2 texel) {
  return BounceLoad(gbuffer_depth, texel).x;
}

// At a depth buffer value `depth` (the trace rebuilds the prepared geometry's positions from their depth).
float3 BounceViewPositionAt(int2 texel, float depth) {
  const float2 uv = (float2(texel) + 0.5f - g_viewport_origin) * g_viewport_dimensions.zw;
  const float4 ndc = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f);
  const float4 position = float4(dot(ndc, inv_projection._m00_m10_m20_m30), dot(ndc, inv_projection._m01_m11_m21_m31),
                                 dot(ndc, inv_projection._m02_m12_m22_m32), dot(ndc, inv_projection._m03_m13_m23_m33));
  return position.xyz / position.w;
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

float3 BounceViewNormal(int2 texel) {
  const float2 angles = BounceLoad(gbuffer_normal, texel).xy * float2(3.14159265f, 6.2831853f)
                        - float2(1.57079633f, 3.14159265f);
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  const float3 world = float3(cosines.y * cosines.x, sines.x, sines.y * cosines.x);
  return normalize(float3(dot(world, view._m00_m10_m20), dot(world, view._m01_m11_m21), dot(world, view._m02_m12_m22)));
}

// As the game's ambient: albedo x (1 - metalness) x 0.96; unlit pixels (flag 1 clear): none.
float3 BounceDiffuseAlbedo(int2 texel) {
  const float4 constants = BounceLoad(gbuffer_constants, texel);
  if ((((uint)round(255.f * constants.w)) & 1u) == 0u) return 0.f;
  return BounceLoad(gbuffer_colour, texel).rgb * (1.f - constants.x) * 0.96f;
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
  return float3(uv * g_viewport_dimensions.xy + g_viewport_origin, length(world - previous_camera_position));
}
