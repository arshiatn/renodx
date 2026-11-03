/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// NVIDIA's NRD's inputs for the sun's shadows (SIGMA, nrd_d3d12.hpp), after the trace (rt_trace.cs_6_5.hlsl: each pixel's
// ray toward a point of the sun's disk: its lit share and the distance to what blocked it): the view depth, the normal
// (from the depth, as the rays' start: no G-buffer at this point of the frame), the penumbra's world radius and the share
// that came through what blocked it (leaves, cloth, smoke, several rays' mean) as its translucency.
// Build (build.ps1, with NRD's Shaders folder): dxc -T cs_6_5 -Fh rt_nrd_front.h -Vn RT_NRD_FRONT.
#include "./rt_scene.hlsli"
#include "NRD.hlsli"

RWTexture2D<float2> sun_rays : register(u2);
RWTexture2D<float> view_z : register(u40);
RWTexture2D<unorm float4> normal_roughness : register(u41);
RWTexture2D<float> penumbra : register(u42);
RWTexture2D<unorm float4> translucency : register(u43);

static const float SKY_VIEW_Z = 1.0e7f;  // metres: past NRD's range (nrd_d3d12.hpp)
static const float LIT_FROM = 0.999f;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id.xy];
  if (position.w <= 0.f) {
    view_z[id.xy] = SKY_VIEW_Z;
    normal_roughness[id.xy] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.f, 1.f, 0.f), 1.f, 0.f);
    penumbra[id.xy] = NRD_FP16_MAX;
    translucency[id.xy] = 1.f;
    return;
  }
  const float2 ray = sun_rays[id.xy];
  const float occluder = (ray.x >= LIT_FROM) ? NRD_FP16_MAX : ray.y;
  view_z[id.xy] = position.w;
  normal_roughness[id.xy] = NRD_FrontEnd_PackNormalAndRoughness(SurfaceNormal(int2(id.xy), position), 1.f, 0.f);
  penumbra[id.xy] = SIGMA_FrontEnd_PackPenumbra(occluder, sun_cone);
  translucency[id.xy] = SIGMA_FrontEnd_PackTranslucency(occluder, ray.x);
}
