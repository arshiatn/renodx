/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The G-buffer guides (ray_tracing.hpp; Ray Reconstruction and the helper's paths read them) from the game's G-buffer as
// its sun pass decodes it (Deferred.shdh DecodeGBufferData, DeferredDirectionalWithSSAO): the diffuse colour (gBuffer3,
// the factor its light multiplies, linear), the specular colour (gBuffer4.rgb), the world normal (gBuffer2's stereographic
// view-space normal, x 1.7777, turned by the view's rotation transposed: the pass's invViewMatrix) and the roughness from
// the Blinn exponent (gBuffer4.a x 255: alpha sqrt(2 / (n + 2)), roughness its square root); the sky (linear depth 1) none.
Texture2D<float> t_linear_depth : register(t0);
Texture2D<float2> t_normals : register(t1);
Texture2D<float4> t_diffuse : register(t2);
Texture2D<float4> t_specular : register(t3);
RWTexture2D<unorm float4> u_diffuse_albedo : register(u0);
RWTexture2D<unorm float4> u_specular_albedo : register(u1);
RWTexture2D<float4> u_normals : register(u2);
RWTexture2D<unorm float> u_roughness : register(u3);

cbuffer rt_guides : register(b0) {
  float4 g_view[3];  // View's rows (its rotation in xyz)
  float4 g_size;     // render width, height
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(float2(id.xy) >= g_size.xy)) return;
  const int3 pixel = int3(id.xy, 0);
  if (!(t_linear_depth.Load(pixel) < 0.99897784f)) {
    u_diffuse_albedo[id.xy] = 0.f;
    u_specular_albedo[id.xy] = 0.f;
    u_normals[id.xy] = 0.f;
    u_roughness[id.xy] = 1.f;
    return;
  }
  const float scale = 1.7777f;
  const float3 encoded = float3(t_normals.Load(pixel) * (2.f * scale) - scale, 1.f);
  const float g = 2.f / dot(encoded, encoded);
  const float3 view_normal = float3(g * encoded.xy, g - 1.f);
  const float3 world_normal = view_normal.x * g_view[0].xyz + view_normal.y * g_view[1].xyz + view_normal.z * g_view[2].xyz;
  const float4 specular = t_specular.Load(pixel);
  u_diffuse_albedo[id.xy] = float4(saturate(t_diffuse.Load(pixel).rgb), 1.f);
  u_specular_albedo[id.xy] = float4(saturate(specular.rgb), 1.f);
  u_normals[id.xy] = float4((dot(world_normal, world_normal) > 0.f) ? normalize(world_normal) : 0.f, 0.f);
  u_roughness[id.xy] = sqrt(sqrt(2.f / (specular.a * 255.f + 2.f)));
}
