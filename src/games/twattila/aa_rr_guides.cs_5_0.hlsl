/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila Ray Reconstruction's guides (anti_aliasing.hpp, at the frame's first sun light draw of the main view: the
// G-buffer bound) for the DLAA bridge: diffuse albedo (the game's ambient: albedo, gamma 2, x 1 - reflectance), specular
// albedo (the game's specular F0 with its Fresnel at this view), world normals and roughness, at the G-buffer's size.
// - F0: src: the game's point lights (tw_point_light.hlsli:103-109, slm; fa9 M10: the game PS :668-669): specular =
//   reflectance (channel 2 w) rising toward saturate(60 x reflectance), times channel 3's rgb (the specular colour); F0 =
//   reflectance x specular colour, its F90 RrSpecularAlbedo's (RRG 4.2.1). deviation: from the game's rise (to
//   saturate(60 x reflectance) along (-l.v)^10, a light-and-view term, not a view Fresnel), because the guide is
//   pre-integrated at the view alone (RRG 4.2.1's EnvBRDFApprox2 takes F0 and its own F90 rule). deviation: skin pixels
//   (stencil & 3 = 3: channel 2 w a Phong scale, channel 3 y the wrap light's share) take the same form, because the
//   guide has no stencil (none bound at the sun draw's slots); fa9 B3 asks their own model: open.
// - Roughness: the traced lobe's linear roughness, sqrt(ReflectAlphaOf(1 - gloss)) (rr_specular.hlsli, the helper's
//   ReflectAlpha: the game's highlight and our rays have that width; src: RRG 3.4.4 linear roughness; NRD's guide the same
//   since a8c251a14), 0 below MIRROR_ROUGHNESS (traced as mirrors), Squared Lobe by g_reflection_squared_lobe.
// - The sky (no normal: the G-buffer cleared): src: RRG 3.4.3 / 3.4.4: normal (0, 0, 0) and roughness 0; NVIDIA's
//   defaults: diffuse albedo 0.5, specular albedo 0.
// - Water (the material flag 4, written again at the reflection pass, after it is drawn: rt_core.hpp Reflections): no
//   diffuse albedo, water's specular albedo, nearly smooth (its own gloss there is no roughness; Softer Water
//   Reflections: at least g_water_roughness).
#include "./rr_specular.hlsli"

// The material flag that marks water (gbuffer_material's alpha x 255); 0u: none (an includer whose water never enters the
// G-buffer and whose alpha means something else).
#ifndef TW_RR_WATER_FLAG
#define TW_RR_WATER_FLAG 4u
#endif

Texture2D<float4> gbuffer_normal : register(t0);    // world normal x 0.5 + 0.5, gloss
Texture2D<float4> gbuffer_colour : register(t1);    // albedo (gamma 2), specular reflectance
Texture2D<float4> gbuffer_material : register(t2);  // rgb: the specular colour (slm); alpha: the material's flags (x 255)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);
cbuffer guides_camera : register(b0) {
  float4x4 g_inverse_view_projection;  // as the depth was drawn (anti_aliasing.hpp GuidesCamera)
  float3 g_camera_position;
  float2 g_inverse_render_size;  // starts the next register, as GuidesCamera's padding keeps it
  float g_water_roughness;       // GuidesCamera::water_roughness (0: 0.03)
  uint g_reflection_squared_lobe;  // GuidesCamera::reflection_squared_lobe (1: Squared Lobe)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 size;
  roughness.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 normal = gbuffer_normal[id.xy];
  const float4 colour = gbuffer_colour[id.xy];
  const float4 material = gbuffer_material[id.xy];
  const float3 albedo = colour.rgb * colour.rgb;
  const bool sky = all(normal.xyz == 0.f);
  const bool water = !sky && (uint(material.a * 255.f + 0.5f) & TW_RR_WATER_FLAG) != 0u;
  diffuse_albedo[id.xy] = float4(sky ? (float3)0.5f : (water ? (float3)0.f : albedo * (1.f - colour.a)), 1.f);
  const float3 world = normal.xyz * 2.f - 1.f;
  const float3 world_normal = (sky || dot(world, world) == 0.f) ? (float3)0.f : normalize(world);
  const float gbuffer_roughness = saturate(1.f - normal.a);
  const float linear_roughness = (gbuffer_roughness < MIRROR_ROUGHNESS) ? 0.f : sqrt(ReflectAlphaOf(gbuffer_roughness, g_reflection_squared_lobe != 0u));
  const float2 uv = (float2(id.xy) + 0.5f) * g_inverse_render_size;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), g_inverse_view_projection);
  const float n_dot_v = dot(world_normal, normalize(g_camera_position - on_ray.xyz / on_ray.w));
  specular_albedo[id.xy] = float4(sky ? (float3)0.f : (water ? (float3)0.02f : RrSpecularAlbedo(colour.a * material.rgb, linear_roughness, n_dot_v)), 1.f);
  normals[id.xy] = float4(world_normal, 0.f);
  roughness[id.xy] = sky ? 0.f : (water ? max(0.0300061f, g_water_roughness) : linear_roughness);
}
