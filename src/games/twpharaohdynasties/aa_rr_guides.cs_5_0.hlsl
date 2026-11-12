/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Pharaoh's Ray Reconstruction guides (twattila/anti_aliasing.hpp: at the frame's first sun light draw of the main view
// from its G-buffer channels 1-3, and again from them after the water, at the sky dome) as its sun light
// (directionallight.hlsl, F883F5B2's bytes) reads the G-buffer. Diffuse albedo: the game's ambient, albedo x (1 - the
// specular colour). Specular albedo: the weight the sun puts on its environment reflection, the specular colour with
// Schlick's Fresnel at this view toward 1 (times 1 - sin(roughness x pi / 2), none below a specular red of 0.1) and
// Smith's visibility at alpha = roughness^2 (the view's cosine as the game takes it, from the camera, saturated).
// Roughness: 1 - gloss, that reflection's. Emissive pixels (flags 2 + 8: the sun draws them 4 x their albedo, unlit):
// neither. Water (flags 64 alone: water_troy 833E7963 writes its G-buffer after the sun draw, albedo 1 and gloss 1): no
// diffuse albedo, its own Fresnel's 0.02, nearly smooth (Softer Water Reflections: at least g_water_roughness).
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
  const float2 encoded = normal.xy * 2.f - 1.f;
  const float y = max(0.f, 1.f - dot(encoded, encoded));
  const float3 world = float3(2.f * encoded.x * sqrt(y), 2.f * y - 1.f, 2.f * encoded.y * sqrt(y));
  // The sun's order: the reflection of the view ray (camera to pixel), then its visibility and Fresnel.
  const float2 uv = (float2(id.xy) + 0.5f) * g_inverse_render_size;
  const float4 on_ray = mul(float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.5f, 1.f), g_inverse_view_projection);
  const float3 view = normalize(on_ray.xyz / on_ray.w - g_camera_position);
  const float n_dot_i = dot(view, world);
  const float3 reflected = world * -(n_dot_i + n_dot_i) + view;
  const float linear_roughness = 1.f - normal.z;
  const float k = 0.5f * (linear_roughness * linear_roughness);
  const float visibility = saturate(0.25f * ((1.f / (saturate(n_dot_i) * (1.f - k) + k)) * (1.f / (saturate(dot(world, reflected)) * (1.f - k) + k))));
  const float cosine = saturate(dot(-view, normalize(world * -(n_dot_i + n_dot_i))));
  const float fresnel = exp2((cosine * -5.55472994f + -6.98316002f) * cosine) * saturate(10.f * specular.x);
  const float3 reflection = ((1.f - specular) * fresnel * (1.f + sin(-1.57079637f * linear_roughness)) + specular) * visibility;
  diffuse_albedo[id.xy] = float4((emissive || water) ? 0.f : gbuffer_colour[id.xy].rgb * (1.f - specular), 1.f);
  specular_albedo[id.xy] = float4(emissive ? 0.f : (water ? 0.02f : saturate(reflection)), 1.f);
  normals[id.xy] = float4(dot(world, world) > 0.f ? normalize(world) : float3(0.f, 1.f, 0.f), 0.f);
  roughness[id.xy] = water ? max(0.01f, g_water_roughness) : saturate(linear_roughness);
}
