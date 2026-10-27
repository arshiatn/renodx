/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_RYSESONOFROME_LIGHT_BOUNCE_HLSLI_
#define SRC_GAMES_RYSESONOFROME_LIGHT_BOUNCE_HLSLI_

// Light Bounce (GT-VBGI, Experimental): Ryse's G-buffer and camera for the shared passes (twattila/light_bounce_*.hlsli),
// as its tiled lighting (cs 0x897232B5) reads them; light_bounce.hpp binds its normals, albedo and linear depth at t42,
// t43 and t45 and its PER_BATCH at b11 (the last frame's at b10). Positions come from the far plane's corners (world
// space from the camera) x the linear depth, as the tiled lighting rebuilds them; the passes work in a view space made
// of the frustum's axes (x right, y up, z forward).
cbuffer PerBatch : register(b11) {
  float4 frustum_top_left;     // the far plane's corners from the camera (world)
  float4 frustum_bottom_left;
  float4 camera_position;
  float4 projection_parameters;
  float4 toward_sun;
  float4 ssdo_parameters;
  float4 frustum_top_right;
  float4 screen_size;          // width, height, 1 / width, 1 / height
};

cbuffer PreviousPerBatch : register(b10) {
  float4 previous_top_left;
  float4 previous_bottom_left;
  float4 previous_camera_position;
  float4 previous_projection_parameters;
  float4 previous_toward_sun;
  float4 previous_ssdo_parameters;
  float4 previous_top_right;
  float4 previous_screen_size;
};

#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer gbuffer_normal : register(t42);  // NormalRT: world normal x 0.5 + 0.5, gloss
BounceGBuffer gbuffer_colour : register(t43);  // DiffuseRT: albedo (gamma 2), material
BounceGBuffer gbuffer_depth : register(t45);   // DepthRT: linear depth / far

// A frustum's axes from its far-plane corners: right, up, forward (away from the camera) and the far plane's distance.
struct BounceFrustum {
  float3 right;
  float3 up;
  float3 forward;
  float far_distance;
};

BounceFrustum FrustumOf(float3 top_left, float3 bottom_left, float3 top_right) {
  BounceFrustum frustum;
  frustum.right = normalize(top_right - top_left);
  frustum.up = normalize(top_left - bottom_left);
  frustum.forward = normalize(cross(frustum.right, frustum.up));
  if (dot(frustum.forward, top_left) < 0.f) frustum.forward = -frustum.forward;
  frustum.far_distance = dot(top_left, frustum.forward);
  return frustum;
}

BounceFrustum CurrentFrustum() {
  return FrustumOf(frustum_top_left.xyz, frustum_bottom_left.xyz, frustum_top_right.xyz);
}

float BounceDepth(int2 texel) {
  return BounceLoad(gbuffer_depth, texel).x;
}

// At a linear depth `depth` (depth / far; the sky's, 1, is nothing: far away).
float3 BounceViewPositionAt(int2 texel, float depth) {
  if (!(depth < 0.999f)) return 1e9f;
  const float2 uv = (float2(texel) + 0.5f) * screen_size.zw;
  const float3 from_camera = (frustum_top_left.xyz + uv.x * (frustum_top_right.xyz - frustum_top_left.xyz)
                              + uv.y * (frustum_bottom_left.xyz - frustum_top_left.xyz)) * depth;
  const BounceFrustum frustum = CurrentFrustum();
  return float3(dot(from_camera, frustum.right), dot(from_camera, frustum.up), dot(from_camera, frustum.forward));
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

float3 BounceViewNormal(int2 texel) {
  const float3 world = normalize(BounceLoad(gbuffer_normal, texel).xyz * 2.f - 1.f);
  const BounceFrustum frustum = CurrentFrustum();
  return normalize(float3(dot(world, frustum.right), dot(world, frustum.up), dot(world, frustum.forward)));
}

// As the tiled lighting's diffuse light takes it: albedo (gamma 2).
float3 BounceDiffuseAlbedo(int2 texel) {
  const float3 colour = BounceLoad(gbuffer_colour, texel).rgb;
  return colour * colour;
}

float BouncePixelsPerMetre(float distance) {
  const float far_height = length(frustum_top_left.xyz - frustum_bottom_left.xyz);
  return screen_size.y * CurrentFrustum().far_distance / (far_height * distance);
}

float2 BounceViewportSize() {
  return screen_size.xy;
}

// The view-space position in the last frame: its pixel (x, y) and its distance from the last frame's camera (z).
float3 BouncePrevious(float3 view_position) {
  const BounceFrustum frustum = CurrentFrustum();
  const float3 world = camera_position.xyz + frustum.right * view_position.x + frustum.up * view_position.y
                       + frustum.forward * view_position.z;
  const float3 from_previous = world - previous_camera_position.xyz;
  const BounceFrustum previous = FrustumOf(previous_top_left.xyz, previous_bottom_left.xyz, previous_top_right.xyz);
  const float depth = dot(from_previous, previous.forward) / previous.far_distance;
  // Behind the last camera (or none yet): off screen.
  if (!(depth > 0.f)) return float3(-1e9f, -1e9f, 0.f);
  const float3 across = previous_top_right.xyz - previous_top_left.xyz;
  const float3 down = previous_bottom_left.xyz - previous_top_left.xyz;
  const float3 on_far = from_previous / depth - previous_top_left.xyz;
  const float2 uv = float2(dot(on_far, across) / dot(across, across), dot(on_far, down) / dot(down, down));
  return float3(uv * screen_size.xy, length(from_previous));
}

#endif  // SRC_GAMES_RYSESONOFROME_LIGHT_BOUNCE_HLSLI_
