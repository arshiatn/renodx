/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer's and Warhammer II's Ray Reconstruction guides (twattila/anti_aliasing.hpp, at the frame's first sun light
// draw of the main view: its G-buffer channels 1-3) from their G-buffer as their sun light reads it (light_bounce.hlsli):
// diffuse albedo the game's ambient (albedo x (1 - specular colour)), specular albedo the specular colour, world
// normals from channel 1's spherical angles (xy), roughness 1 - its z (gloss), at the G-buffer's size.
Texture2D<float4> gbuffer_normal : register(t0);     // world normal (spherical angles in xy), gloss
Texture2D<float4> gbuffer_colour : register(t1);     // albedo
Texture2D<float4> gbuffer_constants : register(t2);  // specular colour
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 normal = gbuffer_normal[id.xy];
  const float3 specular = saturate(gbuffer_constants[id.xy].rgb);
  diffuse_albedo[id.xy] = float4(gbuffer_colour[id.xy].rgb * (1.f - specular), 1.f);
  specular_albedo[id.xy] = float4(specular, 1.f);
  const float2 angles = normal.xy * float2(3.14159265f, 6.2831853f) - float2(1.57079633f, 3.14159265f);
  float2 sines;
  float2 cosines;
  sincos(angles, sines, cosines);
  normals[id.xy] = float4(cosines.y * cosines.x, sines.x, sines.y * cosines.x, 0.f);
  roughness[id.xy] = saturate(1.f - normal.z);
}
