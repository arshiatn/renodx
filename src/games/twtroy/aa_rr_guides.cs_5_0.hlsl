/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Troy's Ray Reconstruction guides (twattila/anti_aliasing.hpp, at the frame's first sun light draw of the main view: its
// G-buffer channels 1-3, and again from them after the water, at the luma statistics F9B80B32) from the G-buffer it shares
// with Pharaoh, as its sun light reads it (Pharaoh's light_bounce.hlsli): diffuse albedo the game's ambient (albedo x
// (1 - specular colour), none where emissive), specular albedo the specular colour as F0 with its Fresnel at this view
// (twattila/rr_specular.hlsli, as Attila's and Warhammer III's), world normals from channel 1's xy, roughness 1 - its z
// (gloss), at the G-buffer's size. Pharaoh's own decode of its sun's reflection weight doesn't hold on
// Troy's bytes (tools\tw\troy\guides). Water (flags 64 alone: all 68 water pixel shaders write it, with albedo 1 and gloss
// 1, after the sun draw; no other G-buffer writer does): no diffuse albedo, its own Fresnel's 0.02, nearly smooth (Softer
// Water Reflections: at least g_water_roughness), as Pharaoh's.
#include "../twattila/rr_specular.hlsli"

Texture2D<float4> gbuffer_normal : register(t0);     // world normal (encoded in xy), gloss
Texture2D<float4> gbuffer_colour : register(t1);     // albedo (an sRGB view: linear here)
Texture2D<float4> gbuffer_constants : register(t2);  // specular colour, flags (w x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);
cbuffer guides_camera : register(b0) {
  float4x4 g_inverse_view_projection;  // as the depth was drawn (anti_aliasing.hpp GuidesCamera)
  float3 g_camera_position;
  float2 g_inverse_render_size;  // starts the next register, as GuidesCamera's padding keeps it
  float g_water_roughness;       // GuidesCamera::water_roughness (0: 0.01)
};

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
  const bool water = flags == 64u;
  diffuse_albedo[id.xy] = float4((emissive || water) ? 0.f : gbuffer_colour[id.xy].rgb * saturate(1.f - specular), 1.f);
  const float2 encoded = normal.xy * 2.f - 1.f;
  const float y = max(0.f, 1.f - dot(encoded, encoded));
  const float3 world = float3(2.f * encoded.x * sqrt(y), 2.f * y - 1.f, 2.f * encoded.y * sqrt(y));
  const float3 world_normal = dot(world, world) > 0.f ? normalize(world) : float3(0.f, 1.f, 0.f);
  const float linear_roughness = saturate(1.f - normal.z);
  const float2 uv = (float2(id.xy) + 0.5f) * g_inverse_render_size;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), g_inverse_view_projection);
  const float n_dot_v = dot(world_normal, normalize(g_camera_position - on_ray.xyz / on_ray.w));
  specular_albedo[id.xy] = float4(water ? (float3)0.02f : RrSpecularAlbedo(saturate(specular), linear_roughness, n_dot_v), 1.f);
  normals[id.xy] = float4(world_normal, 0.f);
  roughness[id.xy] = water ? max(0.01f, g_water_roughness) : linear_roughness;
}
