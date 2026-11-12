/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila Ray Reconstruction's guides (anti_aliasing.hpp, at the frame's first sun light draw of the main view: the
// G-buffer bound) for the DLAA bridge: diffuse albedo (the game's ambient: albedo, gamma 2, x 1 - metalness), specular
// albedo (0.04 toward the albedo with metalness, with its Fresnel at this view), world normals and roughness (1 - gloss),
// at the G-buffer's size. The
// sky (no normal: the G-buffer cleared) takes NVIDIA's defaults: diffuse albedo 0.5, specular albedo 0. Water (the
// material flag 4, written again at the reflection pass, after it is drawn: rt_core.hpp Reflections): no diffuse
// albedo, water's specular albedo, nearly smooth (its own gloss there is no roughness; Softer Water Reflections: at least
// g_water_roughness).
#include "./rr_specular.hlsli"

Texture2D<float4> gbuffer_normal : register(t0);    // world normal x 0.5 + 0.5, gloss
Texture2D<float4> gbuffer_colour : register(t1);    // albedo (gamma 2), metalness
Texture2D<float4> gbuffer_material : register(t2);  // alpha: the material's flags (x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);
cbuffer guides_camera : register(b0) {
  float4x4 g_inverse_view_projection;  // as the depth was drawn (anti_aliasing.hpp GuidesCamera)
  float3 g_camera_position;
  float2 g_inverse_render_size;  // starts the next register, as GuidesCamera's padding keeps it
  float g_water_roughness;       // GuidesCamera::water_roughness (0: 0.03)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 normal = gbuffer_normal[id.xy];
  const float4 colour = gbuffer_colour[id.xy];
  const float3 albedo = colour.rgb * colour.rgb;
  const bool sky = all(normal.xyz == 0.f);
  const bool water = !sky && (uint(gbuffer_material[id.xy].a * 255.f + 0.5f) & 4u) != 0u;
  diffuse_albedo[id.xy] = float4(sky ? (float3)0.5f : (water ? (float3)0.f : albedo * (1.f - colour.a)), 1.f);
  const float3 world = normal.xyz * 2.f - 1.f;
  const float3 world_normal = dot(world, world) > 0.f ? normalize(world) : float3(0.f, 1.f, 0.f);
  const float linear_roughness = saturate(1.f - normal.a);
  const float2 uv = (float2(id.xy) + 0.5f) * g_inverse_render_size;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), g_inverse_view_projection);
  const float n_dot_v = dot(world_normal, normalize(g_camera_position - on_ray.xyz / on_ray.w));
  specular_albedo[id.xy] = float4(sky ? (float3)0.f : (water ? (float3)0.02f : RrSpecularAlbedo(lerp(0.04f, albedo, colour.a), linear_roughness, n_dot_v)), 1.f);
  normals[id.xy] = float4(world_normal, 0.f);
  roughness[id.xy] = water ? max(0.03f, g_water_roughness) : linear_roughness;
}
