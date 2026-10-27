/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila Ray Reconstruction's guides (anti_aliasing.hpp, at the frame's first sun light draw of the main view: the
// G-buffer bound) for the DLAA bridge: diffuse albedo (the game's ambient: albedo, gamma 2, x 1 - metalness), specular
// albedo (0.04 toward the albedo with metalness), world normals and roughness (1 - gloss), at the G-buffer's size.
Texture2D<float4> gbuffer_normal : register(t0);  // world normal x 0.5 + 0.5, gloss
Texture2D<float4> gbuffer_colour : register(t1);  // albedo (gamma 2), metalness
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
  const float4 colour = gbuffer_colour[id.xy];
  const float3 albedo = colour.rgb * colour.rgb;
  diffuse_albedo[id.xy] = float4(albedo * (1.f - colour.a), 1.f);
  specular_albedo[id.xy] = float4(lerp(0.04f, albedo, colour.a), 1.f);
  const float3 world = normal.xyz * 2.f - 1.f;
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 1.f, 0.f), 0.f);
  roughness[id.xy] = saturate(1.f - normal.a);
}
