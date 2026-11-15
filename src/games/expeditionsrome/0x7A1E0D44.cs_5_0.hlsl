/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private ray tracing shader (ray_tracing.hpp), not a game replacement registration: the helper's G-buffer guides (its
// reflections and paths read them) from Unreal 4.26's G-buffer at the sun's deferred light draw: diffuse albedo (base
// colour x (1 - metallic)), specular albedo (F0: 0.08 x specular toward the base colour with metallic), world normals
// (none where unlit) and roughness, at the render pixels.
Texture2D<float4> gbuffer_a : register(t0);  // world normal x 0.5 + 0.5
Texture2D<float4> gbuffer_b : register(t1);  // metallic, specular, roughness, shading model | flags << 4 (/ 255)
Texture2D<float4> gbuffer_c : register(t2);  // base colour (its sRGB view: linear)
RWTexture2D<unorm float4> diffuse_albedo : register(u0);
RWTexture2D<unorm float4> specular_albedo : register(u1);
RWTexture2D<float4> normals : register(u2);
RWTexture2D<unorm float> roughness : register(u3);

cbuffer guides : register(b0) {
  uint4 render_size;  // xy
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= render_size.xy)) return;
  const float3 world = gbuffer_a[id.xy].xyz * 2.f - 1.f;
  const float4 surface = gbuffer_b[id.xy];
  const float3 base = gbuffer_c[id.xy].rgb;
  const bool lit = (uint(round(surface.a * 255.f)) & 15u) != 0u;
  diffuse_albedo[id.xy] = float4(base * (1.f - surface.r), 1.f);
  specular_albedo[id.xy] = float4(lerp(0.08f * surface.g, base, surface.r), 1.f);
  normals[id.xy] = float4((lit && dot(world, world) > 0.f) ? normalize(world) : 0.f, 0.f);
  roughness[id.xy] = surface.b;
}
