/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The G-buffer guides (ray_tracing.hpp; Ray Reconstruction, the paths and the reflections read them) from the G-buffer as
// DeferredSky lights it (PBR.shdh DecodeGBufferData, 518005B9): the diffuse colour (GBuffer2 x (1 - the metal mask)), the
// specular colour as its IBL takes it (F0 = lerp(0.16 x reflectance^2, base colour, metal) x the LUT's scale + its bias at
// N.V and the roughness), the world normal (GBuffer1's stereographic view-space normal, x 3.5554 - 1.7777, turned by the
// View's rotation transposed: its invViewMatrix) and the roughness the game lights with (max(0.09, GBuffer3.x),
// perceptual: GGX's alpha its square, PBR.shdh); the sky (linear depth 1) none.
#include "./rt_ibl.hlsli"

RWTexture2D<unorm float4> u_diffuse_albedo : register(u0);
RWTexture2D<unorm float4> u_specular_albedo : register(u1);
RWTexture2D<float4> u_normals : register(u2);
RWTexture2D<unorm float> u_roughness : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= ibl_size.xy)) return;
  const IblSurface surface = ReadIblSurface(id.xy);
  if (!surface.lit) {
    u_diffuse_albedo[id.xy] = 0.f;
    u_specular_albedo[id.xy] = 0.f;
    u_normals[id.xy] = 0.f;
    u_roughness[id.xy] = 1.f;
    return;
  }
  u_diffuse_albedo[id.xy] = float4(saturate(surface.diffuse), 1.f);
  u_specular_albedo[id.xy] = float4(saturate(surface.specular), 1.f);
  u_normals[id.xy] = float4(surface.normal, 0.f);
  u_roughness[id.xy] = surface.roughness;
}
