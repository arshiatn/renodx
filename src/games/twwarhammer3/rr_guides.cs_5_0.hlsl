/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's G-buffer guides (taa_dlss.hpp: Ray Reconstruction's and the rays') for the DLSS bridge, as its deferred sun
// light and its reflection resolve read them: the colour's share by metal, specular from the dielectric's 0.08 x colour alpha
// to the colour by metal, roughness in the material's y, world normals (the view-space normal from all 10 bits of each
// channel around the view axis, turned by inv_view). Water (the flags' class 64, drawn after the first guides: its rays
// rewrite them): the resolve's own 0.02 and gloss, no diffuse.
Texture2D<float4> gbuffer_normal : register(t0);    // channel 1
Texture2D<float4> gbuffer_colour : register(t1);    // channel 2
Texture2D<float4> gbuffer_material : register(t2);  // channel 3: x metal, y roughness, alpha the flags (x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

cbuffer camera : register(b0) {
  float4 camera[21];  // c18-c20: inv_view's rows (acting on column vectors)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float2 e = gbuffer_normal[id.xy].xy * 2.f - 1.f;
  const float rest = 1.f - dot(e, e);
  const float3 view = float3(2.f * e * sqrt(max(rest, 0.f)), 1.f - 2.f * rest);
  const float3 world = float3(dot(view, camera[18].xyz), dot(view, camera[19].xyz), dot(view, camera[20].xyz));
  const float4 colour = gbuffer_colour[id.xy];
  const float4 material = gbuffer_material[id.xy];
  const bool water = (uint(round(material.a * 255.f)) & 96u) == 64u;
  const float3 specular = water ? (float3)0.02f : lerp((float3)(colour.a * 0.08f), colour.rgb, material.x);
  diffuse_albedo[id.xy] = float4(water ? (float3)0.f : colour.rgb * (1.f - material.x) * (1.f - specular), 1.f);
  specular_albedo[id.xy] = float4(specular, 1.f);
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 0.f, 1.f), 0.f);
  roughness[id.xy] = water ? 0.01f : saturate(material.y);
}
