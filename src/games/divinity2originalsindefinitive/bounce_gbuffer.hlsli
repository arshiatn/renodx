/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_BOUNCE_GBUFFER_HLSLI_
#define SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_BOUNCE_GBUFFER_HLSLI_

// GT-VBGI (screen_space.hpp): Divinity 2's G-buffer and camera for the shared passes (twattila/light_bounce_*.hlsli) as
// talesofarise_dlss/ue_light_bounce.hpp binds them (its host_light_bounce_* wrappers here): GBuffer1 (the view-space
// normal, stereographic / 1.7777) at t42, GBuffer2 (base colour) at t43, GBuffer3 (roughness, reflectance, metal mask,
// shading model << 1) at t44, LinearizeDepth's depth (view z / the far plane, 1 on the sky) at t45, the camera at b11
// (screen_space.hpp BounceView: PerView's main camera as dlaa.hpp keeps it, and the last frame's). Positions in metres,
// in PerView's camera space: x right, y up, z forward.
#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer d2_gbuffer1 : register(t42);
BounceGBuffer d2_gbuffer2 : register(t43);
BounceGBuffer d2_gbuffer3 : register(t44);
BounceGBuffer d2_linear_depth : register(t45);

cbuffer bounce_view : register(b11) {
  float4 bounce_view_rows[3];                 // the View's rows (world to camera space: rotation, translation)
  float4 bounce_projection_rows[2];           // the projection's rows 0 and 1, as the depth was drawn (the jitter in z)
  float4 bounce_previous_view_projection[4];  // the last frame's rows (acting on column vectors)
  float4 bounce_eye;                          // xyz the camera's position, w the far plane
  float4 bounce_previous_eye;                 // xyz the last frame's camera position
  float4 bounce_size;                         // xy the render size
};

float BounceDepth(int2 texel) {
  return BounceLoad(d2_linear_depth, texel).x;
}

// The projection's rows 0 and 1 undone at the pixel's depth; the sky far away.
float3 BounceViewPositionAt(int2 texel, float depth) {
  if (!(depth < 1.f)) return 1e9f;
  const float z = depth * bounce_eye.w;
  const float2 ndc = (float2(texel) + 0.5f) / bounce_size.xy * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float x = (ndc.x * z - bounce_projection_rows[0].z * z - bounce_projection_rows[0].w) / bounce_projection_rows[0].x;
  const float y = (ndc.y * z - bounce_projection_rows[1].z * z - bounce_projection_rows[1].w - bounce_projection_rows[1].x * x)
                  / bounce_projection_rows[1].y;
  return float3(x, y, z);
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

float3 BounceViewNormal(int2 texel) {
  const float3 encoded = float3(BounceLoad(d2_gbuffer1, texel).xy * 3.5554f - 1.7777f, 1.f);
  const float g = 2.f / dot(encoded, encoded);
  return normalize(float3(g * encoded.xy, g - 1.f));
}

// The diffuse colour DeferredSky's IBL takes (rt_ibl.hlsli): base colour x (1 - metal); the unlit shading model none.
float3 BounceDiffuseAlbedo(int2 texel) {
  const float4 material = BounceLoad(d2_gbuffer3, texel);
  if (((uint(round(material.w * 255.f)) >> 1u) & 3u) == 1u) return 0.f;
  return BounceLoad(d2_gbuffer2, texel).rgb * (1.f - material.z);
}

float BouncePixelsPerMetre(float distance) {
  return 0.5f * bounce_size.y * bounce_projection_rows[1].y / distance;
}

float2 BounceViewportSize() {
  return bounce_size.xy;
}

// The camera-space position in the last frame: its pixel (x, y) and its distance from the last frame's camera (z).
float3 BouncePrevious(float3 view_position) {
  const float3 moved = view_position - float3(bounce_view_rows[0].w, bounce_view_rows[1].w, bounce_view_rows[2].w);
  const float4 world = float4(moved.x * bounce_view_rows[0].xyz + moved.y * bounce_view_rows[1].xyz + moved.z * bounce_view_rows[2].xyz, 1.f);
  const float4 clip = float4(dot(bounce_previous_view_projection[0], world), dot(bounce_previous_view_projection[1], world),
                             dot(bounce_previous_view_projection[2], world), dot(bounce_previous_view_projection[3], world));
  // Behind the last camera: off screen.
  if (!(clip.w > 0.f)) return float3(-1e9f, -1e9f, 0.f);
  const float2 uv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
  return float3(uv * bounce_size.xy, distance(world.xyz, bounce_previous_eye.xyz));
}

#endif  // SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_BOUNCE_GBUFFER_HLSLI_
