/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// A G-buffer pixel as DeferredSky's IBL lights it (518005B9; PBR.shdh DecodeGBufferData): rt_guides and rt_reflect_apply
// (ray_tracing.hpp). Its resources are ClusteredLightingWithSSAO's G-buffer and DeferredSky's LUT (t3 there, kept); the
// view from the main camera (dlaa.hpp: View's rows, the jittered view-projection's inverse, the eye).
#ifndef SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_RT_IBL_HLSLI_
#define SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_RT_IBL_HLSLI_

Texture2D<float> ibl_linear_depth : register(t0);  // 1: the far plane
Texture2D<float2> ibl_gbuffer1 : register(t1);     // the view-space normal, stereographic / 1.7777
Texture2D<float4> ibl_gbuffer2 : register(t2);     // base colour, fade
Texture2D<float4> ibl_gbuffer3 : register(t3);     // roughness, reflectance, metal mask, shading model << 1 (/ 255)
Texture2D<float4> ibl_lut : register(t4);          // DeferredSky's: x the F0 scale, y its bias at (N.V, roughness)
SamplerState ibl_linear : register(s0);

cbuffer ibl_view : register(b0) {
  float4 ibl_view_rows[3];                 // the View's rows (its rotation in xyz)
  float4 ibl_inverse_view_projection[4];  // rows acting on column vectors (the jittered camera's, as the depth was drawn)
  float4 ibl_eye;                          // xyz the camera's position
  uint4 ibl_size;                          // xy the render size
};

struct IblSurface {
  bool lit;  // not the far plane, not the unlit shading model (DeferredSky's own test)
  float3 normal;
  float3 diffuse;   // base colour x (1 - metal)
  float3 specular;  // F0 x the LUT's scale + its bias
  float roughness;  // max(0.09, GBuffer3.x)
};

IblSurface ReadIblSurface(uint2 pixel) {
  IblSurface surface = (IblSurface)0;
  const int3 at = int3(pixel, 0);
  const float4 gbuffer3 = ibl_gbuffer3.Load(at);
  const uint shading_model = (uint(round(gbuffer3.w * 255.f)) >> 1u) & 3u;
  surface.lit = ibl_linear_depth.Load(at) != 1.f && shading_model != 1u;
  if (!surface.lit) return surface;
  const float3 encoded = float3(ibl_gbuffer1.Load(at) * 3.5554f - 1.7777f, 1.f);
  const float g = 2.f / dot(encoded, encoded);
  const float3 view_normal = float3(g * encoded.xy, g - 1.f);
  surface.normal = normalize(view_normal.x * ibl_view_rows[0].xyz + view_normal.y * ibl_view_rows[1].xyz + view_normal.z * ibl_view_rows[2].xyz);
  // Toward the eye along the pixel's ray (a point on it from the inverse view-projection).
  const float2 ndc = (float2(pixel) + .5f) / float2(ibl_size.xy) * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float4 clip = float4(ndc, .5f, 1.f);
  const float4 point_on_ray = float4(dot(ibl_inverse_view_projection[0], clip), dot(ibl_inverse_view_projection[1], clip),
                                     dot(ibl_inverse_view_projection[2], clip), dot(ibl_inverse_view_projection[3], clip));
  const float3 toward_eye = normalize(ibl_eye.xyz - point_on_ray.xyz / point_on_ray.w);
  const float3 base = ibl_gbuffer2.Load(at).rgb;
  const float metal = gbuffer3.z;
  surface.diffuse = base * (1.f - metal);
  const float3 f0 = metal * base + (1.f - metal) * (0.16f * gbuffer3.y * gbuffer3.y);
  surface.roughness = max(0.09f, gbuffer3.x);
  const float2 lut = ibl_lut.SampleLevel(ibl_linear, float2(max(0.001953125f, dot(surface.normal, toward_eye)), surface.roughness), 0.f).xy;
  surface.specular = f0 * lut.x + lut.y;
  return surface;
}

#endif  // SRC_GAMES_DIVINITY2ORIGINALSINDEFINITIVE_RT_IBL_HLSLI_
