/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Troy's and Pharaoh's Ray Reconstruction guides (twattila/anti_aliasing.hpp, at the frame's first sun light draw of the
// main view: its G-buffer channels 1-3) from their G-buffer as their sun light reads it (light_bounce.hlsli): diffuse
// albedo the game's ambient (albedo x (1 - specular colour), none where emissive), specular albedo the specular colour,
// world normals from channel 1's xy, roughness 1 - its z (gloss), at the G-buffer's size.
Texture2D<float4> gbuffer_normal : register(t0);     // world normal (encoded in xy), gloss
Texture2D<float4> gbuffer_colour : register(t1);     // albedo (an sRGB view: linear here)
Texture2D<float4> gbuffer_constants : register(t2);  // specular colour, flags (w x 255)
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
  const float4 constants = gbuffer_constants[id.xy];
  const uint flags = (uint)round(255.f * constants.w);
  const float3 specular = float3(constants.xy, ((flags & 128u) != 0u) ? constants.x : constants.z);
  const bool emissive = (flags & 10u) == 10u;
  diffuse_albedo[id.xy] = float4(emissive ? 0.f : gbuffer_colour[id.xy].rgb * saturate(1.f - specular), 1.f);
  specular_albedo[id.xy] = float4(saturate(specular), 1.f);
  const float2 encoded = normal.xy * 2.f - 1.f;
  const float y = max(0.f, 1.f - dot(encoded, encoded));
  const float3 world = float3(2.f * encoded.x * sqrt(y), 2.f * y - 1.f, 2.f * encoded.y * sqrt(y));
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 1.f, 0.f), 0.f);
  roughness[id.xy] = saturate(1.f - normal.z);
}
