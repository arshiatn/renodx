/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III Ray Reconstruction's guides (taa_dlss.hpp, at the frame's first sun light draw: its G-buffer bound) for
// the DLSS bridge, as its deferred sun light (directionallight.hlsl) reads them, dielectric only: diffuse albedo (the
// colour x 0.96), specular albedo (0.04), world normals (the view-space normal from the upper 8 of each 10 bits around
// the view axis, turned by inv_view) and roughness (1 - the material channel's z).
Texture2D<float4> gbuffer_normal : register(t0);    // channel 1
Texture2D<float4> gbuffer_colour : register(t1);    // channel 2
Texture2D<float4> gbuffer_material : register(t2);  // channel 3
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
  const uint2 encoded = uint2(round(gbuffer_normal[id.xy].xy * 1023.f)) >> 2;
  const float2 e = float2(encoded) * (2.f / 255.f) - 1.f;
  const float rest = 1.f - dot(e, e);
  const float3 view = float3(2.f * e * sqrt(max(rest, 0.f)), 1.f - 2.f * rest);
  const float3 world = float3(dot(view, camera[18].xyz), dot(view, camera[19].xyz), dot(view, camera[20].xyz));
  diffuse_albedo[id.xy] = float4(gbuffer_colour[id.xy].rgb * 0.96f, 1.f);
  specular_albedo[id.xy] = float4(0.04f.xxx, 1.f);
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 0.f, 1.f), 0.f);
  roughness[id.xy] = saturate(1.f - gbuffer_material[id.xy].z);
}
