/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's G-buffer guides (taa_dlss.hpp: Ray Reconstruction's and the rays') for the DLSS bridge, as its deferred sun
// light and its reflection resolve read them: the colour's share by metal, specular from the dielectric's F0 to the colour by
// metal with its Fresnel at this view (the camera's rays as BounceViewPositionAt, no depth needed), roughness in the material's y,
// world normals (the view-space normal from all 10 bits of each channel around the view
// axis, turned by inv_view). Water (the flags' class 64, drawn after the first guides: its rays rewrite them): the resolve's
// own 0.02 and gloss, no diffuse. The sun shaders differ (as in light_bounce.hlsli): diffuse_rule 1: F0 0.08 x colour alpha;
// 2: F0 0.04; 0: F0 0.04, no metal and no flags in this G-buffer, roughness in the material's z, the normal in the upper 8 of
// each 10 bits. Nothing drawn (no unit normal to rebuild): the bare F0.
#include "../twattila/rr_specular.hlsli"

Texture2D<float4> gbuffer_normal : register(t0);    // channel 1
Texture2D<float4> gbuffer_colour : register(t1);    // channel 2
Texture2D<float4> gbuffer_material : register(t2);  // channel 3: x metal, y roughness, alpha the flags (x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

cbuffer camera : register(b0) {
  float4x4 inv_view : packoffset(c18);
  float4x4 inv_projection : packoffset(c26);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
};

cbuffer guides_settings : register(b1) {
  float diffuse_rule;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float2 stored = gbuffer_normal[id.xy].xy;
  const float2 e = (diffuse_rule > 0.f) ? (stored * 2.f - 1.f) : (float2(uint2(round(stored * 1023.f)) >> 2) * (2.f / 255.f) - 1.f);
  const float rest = 1.f - dot(e, e);
  const float3 view = float3(2.f * e * sqrt(max(rest, 0.f)), 1.f - 2.f * rest);
  const float3 world = mul(view, (float3x3)inv_view);
  const float4 colour = gbuffer_colour[id.xy];
  const float4 material = gbuffer_material[id.xy];
  const bool has_material = diffuse_rule > 0.f;
  const bool water = has_material && (uint(round(material.a * 255.f)) & 96u) == 64u;
  const float metal = has_material ? material.x : 0.f;
  const float dielectric = (diffuse_rule == 1.f) ? colour.a * 0.08f : 0.04f;
  const float3 specular = water ? (float3)0.02f : lerp((float3)dielectric, colour.rgb, metal);
  diffuse_albedo[id.xy] = float4(water ? (float3)0.f : colour.rgb * (1.f - metal) * (1.f - specular), 1.f);
  const float linear_roughness = saturate(has_material ? material.y : material.z);
  const float2 uv = (float2(id.xy) + 0.5f - g_viewport_origin + g_vpos_texel_offset) * g_viewport_dimensions.zw;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), inv_projection);
  const float n_dot_v = dot(view, normalize(on_ray.xyz / on_ray.w));
  specular_albedo[id.xy] = float4((water || rest < 0.f) ? specular : RrSpecularAlbedo(specular, linear_roughness, n_dot_v), 1.f);
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 0.f, 1.f), 0.f);
  roughness[id.xy] = water ? 0.01f : linear_roughness;
}
