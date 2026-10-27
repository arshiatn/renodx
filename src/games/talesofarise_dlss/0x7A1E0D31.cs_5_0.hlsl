/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// DLSS Ray Reconstruction's guides from UE 4.21's G-buffer (copied at Bandai's lighting pass; the render size): the
// diffuse albedo (base colour x (1 - metallic)), the specular albedo (F0 = lerp(0.08 x specular, base colour, metallic)
// through Karis' analytical environment BRDF at the pixel's N.V), the world normal and the roughness. Sky and unlit
// pixels (no normal): zero.
Texture2D<float4> GBufferA : register(t0);  // world normal x 0.5 + 0.5
Texture2D<float4> GBufferB : register(t1);  // metallic, specular, roughness, shading model
Texture2D<float4> GBufferC : register(t2);  // base colour
RWTexture2D<float4> DiffuseAlbedo : register(u0);
RWTexture2D<float4> SpecularAlbedo : register(u1);
RWTexture2D<float4> Normals : register(u2);
RWTexture2D<float> Roughness : register(u3);

cbuffer Guides : register(b0) {
  float4 world_to_view[3];  // View.TranslatedWorldToView's rows (a rotation for directions)
  float2 projection;        // View.ViewToClip _m00, _m11
  uint2 extent;             // the render size
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= extent)) return;
  const int3 pixel = int3(id.xy, 0);
  const float3 encoded = GBufferA.Load(pixel).xyz;
  const float4 material = GBufferB.Load(pixel);
  const float3 base_colour = GBufferC.Load(pixel).rgb;
  float3 normal = encoded * 2.f - 1.f;
  const float normal_length = length(normal);
  if (!(normal_length > 0.1f)) {
    DiffuseAlbedo[id.xy] = 0.f;
    SpecularAlbedo[id.xy] = 0.f;
    Normals[id.xy] = 0.f;
    Roughness[id.xy] = 1.f;
    return;
  }
  normal /= normal_length;
  const float metallic = saturate(material.r);
  const float specular = saturate(material.g);
  const float roughness = saturate(material.b);
  const float3 f0 = lerp(0.08f * specular, base_colour, metallic);
  // N.V with the pixel's view ray (view x right, y up, z forward).
  const float2 ndc = (float2(id.xy) + 0.5f) / float2(extent) * float2(2.f, -2.f) + float2(-1.f, 1.f);
  const float3 view_ray = normalize(float3(ndc / projection, 1.f));
  const float3 view_normal = normal.x * world_to_view[0].xyz + normal.y * world_to_view[1].xyz + normal.z * world_to_view[2].xyz;
  const float n_dot_v = saturate(dot(view_normal, -view_ray));
  // Karis 2014, "Physically Based Shading on Mobile".
  const float4 c0 = float4(-1.f, -0.0275f, -0.572f, 0.022f);
  const float4 c1 = float4(1.f, 0.0425f, 1.04f, -0.04f);
  const float4 r = roughness * c0 + c1;
  const float a004 = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
  const float2 scale_bias = float2(-1.04f, 1.04f) * a004 + r.zw;
  DiffuseAlbedo[id.xy] = float4(base_colour * (1.f - metallic), 1.f);
  SpecularAlbedo[id.xy] = float4(saturate(f0 * scale_bias.x + scale_bias.y), 1.f);
  Normals[id.xy] = float4(normal, 0.f);
  Roughness[id.xy] = roughness;
}
