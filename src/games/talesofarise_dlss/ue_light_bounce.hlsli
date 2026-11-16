/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_DLSS_UE_LIGHT_BOUNCE_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_UE_LIGHT_BOUNCE_HLSLI_

// Light Bounce (GT-VBGI, Experimental): an Unreal game's G-buffer and View for the shared passes
// (twattila/light_bounce_*.hlsli), as ue_light_bounce.hpp binds them: GBufferA (world normal x 0.5 + 0.5) at t42,
// GBufferC (base colour) at t43, GBufferB (metallic, specular, roughness, shading model | flags << 4) at t44, the scene
// depth (device Z, reversed: the sky 0) at t45, the View at b11 (ue_view.hlsli; the includer defines UE_VIEW_SHIFT). Its
// Prev* rows give the last frame's camera. Positions in metres (Unreal's centimetres / 100), in Unreal's view space:
// x right, y up, z forward. UE_BOUNCE_IRRADIANCE (the includer's apply): the diffuse albedo 1, for a game whose own
// lighting pass multiplies its base colour into what the apply writes.
#define UE_VIEW_REGISTER b11
#include "./ue_view.hlsli"
#include "../twattila/light_bounce_gbuffer.hlsli"

BounceGBuffer ue_gbuffer_a : register(t42);
BounceGBuffer ue_gbuffer_c : register(t43);
BounceGBuffer ue_gbuffer_b : register(t44);
BounceGBuffer ue_scene_depth : register(t45);

float BounceDepth(int2 texel) {
  return BounceLoad(ue_scene_depth, texel).x;
}

// At a device Z `depth` (0, the sky or nothing: far away). ViewToClip's x and y (the TAA jitter in its third row)
// undone at the pixel's scene depth.
float3 BounceViewPositionAt(int2 texel, float depth) {
  if (!(depth > 0.f)) return 1e9f;
  const float4 transform = UE_VIEW(UE_INV_DEVICE_Z_TO_WORLD_Z);
  const float scene_depth = depth * transform.x + transform.y + 1.f / (depth * transform.z - transform.w);
  const float4 scale_bias = UE_VIEW(UE_SCREEN_POSITION_SCALE_BIAS);
  const float2 uv = (float2(texel) + 0.5f) * UE_VIEW(UE_BUFFER_SIZE_AND_INV_SIZE).zw;
  const float2 screen = (uv - scale_bias.wz) / scale_bias.xy;
  const float4 jitter_row = UE_VIEW(UE_VIEW_TO_CLIP + 2u);
  const float2 focal = float2(UE_VIEW(UE_VIEW_TO_CLIP).x, UE_VIEW(UE_VIEW_TO_CLIP + 1u).y);
  return float3(scene_depth * (screen - jitter_row.xy) / focal, scene_depth) * 0.01f;
}

float3 BounceViewPosition(int2 texel) {
  return BounceViewPositionAt(texel, BounceDepth(texel));
}

float3 BounceViewNormal(int2 texel) {
  const float3 world = normalize(BounceLoad(ue_gbuffer_a, texel).xyz * 2.f - 1.f);
  return normalize(world.x * UE_VIEW(UE_TRANSLATED_WORLD_TO_VIEW).xyz + world.y * UE_VIEW(UE_TRANSLATED_WORLD_TO_VIEW + 1u).xyz
                   + world.z * UE_VIEW(UE_TRANSLATED_WORLD_TO_VIEW + 2u).xyz);
}

// Unreal's diffuse colour: base colour x (1 - metallic); the unlit shading model (0) none.
float3 BounceDiffuseAlbedo(int2 texel) {
#ifdef UE_BOUNCE_IRRADIANCE
  return 1.f;
#else
  const float4 material = BounceLoad(ue_gbuffer_b, texel);
  if ((uint(round(material.a * 255.f)) & 15u) == 0u) return 0.f;
  return BounceLoad(ue_gbuffer_c, texel).rgb * (1.f - material.r);
#endif
}

float BouncePixelsPerMetre(float distance) {
  return 0.5f * UE_VIEW(UE_VIEW_SIZE_AND_INV_SIZE).y * UE_VIEW(UE_VIEW_TO_CLIP + 1u).y / distance;
}

float2 BounceViewportSize() {
  return UE_VIEW(UE_VIEW_SIZE_AND_INV_SIZE).xy;
}

// The view-space position in the last frame: its pixel (x, y) and its distance from the last frame's camera (z). The
// translated world moves with the camera: the same point is this frame's minus PreViewTranslation plus last frame's.
float3 BouncePrevious(float3 view_position) {
  const float3 view = view_position * 100.f;
  const float3 translated = view.x * UE_VIEW(UE_VIEW_TO_TRANSLATED_WORLD).xyz + view.y * UE_VIEW(UE_VIEW_TO_TRANSLATED_WORLD + 1u).xyz
                            + view.z * UE_VIEW(UE_VIEW_TO_TRANSLATED_WORLD + 2u).xyz + UE_VIEW(UE_VIEW_TO_TRANSLATED_WORLD + 3u).xyz;
  const float3 previous = translated - UE_VIEW(UE_PRE_VIEW_TRANSLATION).xyz + UE_VIEW(UE_PREV_PRE_VIEW_TRANSLATION).xyz;
  const float4 clip = previous.x * UE_VIEW(UE_PREV_TRANSLATED_WORLD_TO_CLIP) + previous.y * UE_VIEW(UE_PREV_TRANSLATED_WORLD_TO_CLIP + 1u)
                      + previous.z * UE_VIEW(UE_PREV_TRANSLATED_WORLD_TO_CLIP + 2u) + UE_VIEW(UE_PREV_TRANSLATED_WORLD_TO_CLIP + 3u);
  // Behind the last camera: off screen.
  if (!(clip.w > 0.f)) return float3(-1e9f, -1e9f, 0.f);
  const float4 scale_bias = UE_VIEW(UE_SCREEN_POSITION_SCALE_BIAS);
  const float2 uv = clip.xy / clip.w * scale_bias.xy + scale_bias.wz;
  return float3(uv * UE_VIEW(UE_BUFFER_SIZE_AND_INV_SIZE).xy, length(previous) * 0.01f);
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_UE_LIGHT_BOUNCE_HLSLI_
