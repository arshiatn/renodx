/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing's G-buffer guides (ray_tracing.hpp; the helper's reflections and paths read them) from Shogun 2's G-buffer
// as its sun light reads it: diffuse albedo (colour x (1 - metallic) x 0.96), specular albedo (0.04 toward the colour
// with metallic), world normals from the normal's spherical angles (xy), roughness 1 - the constants' y.
Texture2D<float4> gbuffer_normal : register(t0);     // spherical angles in xy
Texture2D<float4> gbuffer_colour : register(t1);
Texture2D<float4> gbuffer_constants : register(t2);  // metallic, gloss
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float3 colour = gbuffer_colour[id.xy].rgb;
  const float4 constants = gbuffer_constants[id.xy];
  diffuse_albedo[id.xy] = float4(colour * (1.f - constants.x) * 0.96f, 1.f);
  specular_albedo[id.xy] = float4(lerp(0.04f, colour, constants.x), 1.f);
  const float2 angles = gbuffer_normal[id.xy].xy * float2(3.14159265f, 6.2831853f) - float2(1.57079633f, 3.14159265f);
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  normals[id.xy] = float4(cosines.y * cosines.x, sines.x, sines.y * cosines.x, 0.f);
  roughness[id.xy] = saturate(1.f - constants.y);
}
