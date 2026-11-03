/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// NVIDIA's NRD's inputs (nrd_d3d12.hpp), after what each denoises was traced, on the trace's bindings. The view depth and
// the normal (from the depth as the rays' start, or the G-buffer's where a request has it) always. NRD_FRONT_SUN (SIGMA;
// rt_trace.cs_6_5.hlsl: each pixel's ray toward a point of the sun's disk, its lit share and blocker's distance): the
// penumbra's world radius and the share that came through what blocked it (leaves, cloth, smoke, several rays' mean) as its
// translucency. NRD_FRONT_AMBIENT (REBLUR's occlusion): the rays' open share as the normalized hit distance (the range is
// its 1). NRD_FRONT_BOUNCE (REBLUR on the Light Bounce, a texel each bounce_scale render pixels): its light and hit
// distance. NRD_FRONT_REFLECT (REBLUR on the reflections): their light and hit distance packed in place.
// Build (build.ps1, with NRD's Shaders folder): dxc -T cs_6_5 -D NRD_FRONT_<signal> -Vn RT_NRD_FRONT_<signal>.
#include "./rt_scene.hlsli"
#include "NRD.hlsli"

static const float SKY_VIEW_Z = 1.0e7f;  // metres: past NRD's range (nrd_d3d12.hpp)
// As nrd::ReblurHitDistanceParameters' (the bounce and the reflections keep its defaults).
static const float3 HIT_DISTANCE_PARAMETERS = float3(3.f, 0.1f, 20.f);

#if defined(NRD_FRONT_SUN)
RWTexture2D<float2> sun_rays : register(u2);
RWTexture2D<float> view_z : register(u40);
RWTexture2D<unorm float4> normal_roughness : register(u41);
RWTexture2D<float> penumbra : register(u42);
RWTexture2D<unorm float4> translucency : register(u43);
static const float LIT_FROM = 0.999f;
#elif defined(NRD_FRONT_BOUNCE)
RWTexture2D<float4> bounce : register(u9);
RWTexture2D<float> hit_distance : register(u44);
RWTexture2D<float> view_z : register(u45);
RWTexture2D<unorm float4> normal_roughness : register(u46);
RWTexture2D<float4> signal : register(u47);
cbuffer Bounce : register(b2) {
  float4 view_projection[4];
  uint bounce_width;
  uint bounce_height;
  uint bounce_scale;
  uint bounce_padding;
};
#elif defined(NRD_FRONT_REFLECT)
Texture2D<float4> guide_normals : register(t12);  // world normals (x 0: none)
Texture2D<float> guide_roughness : register(t14);
RWTexture2D<float> view_z : register(u48);
RWTexture2D<unorm float4> normal_roughness : register(u49);
RWTexture2D<float4> signal : register(u50);  // in: light x the share that met, metres; out: packed
#elif defined(NRD_FRONT_AMBIENT)
RWTexture2D<unorm float> ambient : register(u1);
RWTexture2D<float> view_z : register(u51);
RWTexture2D<unorm float4> normal_roughness : register(u52);
RWTexture2D<unorm float> signal : register(u53);
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
#if defined(NRD_FRONT_BOUNCE)
  if (id.x >= bounce_width || id.y >= bounce_height) return;
  const int2 pixel = min(int2(id.xy * bounce_scale + bounce_scale / 2u), int2(width, height) - 1);
#else
  if (id.x >= width || id.y >= height) return;
  const int2 pixel = int2(id.xy);
#endif
  const float4 position = positions[pixel];
  if (position.w <= 0.f) {
    view_z[id.xy] = SKY_VIEW_Z;
    normal_roughness[id.xy] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.f, 1.f, 0.f), 1.f, 0.f);
#if defined(NRD_FRONT_SUN)
    penumbra[id.xy] = NRD_FP16_MAX;
    translucency[id.xy] = 1.f;
#else
    signal[id.xy] = 0.f;
#endif
    return;
  }
  view_z[id.xy] = position.w;
#if defined(NRD_FRONT_REFLECT)
  const float3 guide = guide_normals[pixel].xyz;
  const float3 normal = (dot(guide, guide) > 0.5f) ? normalize(guide) : SurfaceNormal(pixel, position);
  const float roughness = saturate(guide_roughness[pixel]);
  const float4 traced = signal[id.xy];
  signal[id.xy] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(traced.rgb, REBLUR_FrontEnd_GetNormHitDist(traced.a, position.w, HIT_DISTANCE_PARAMETERS, roughness), true);
#else
  const float3 normal = SurfaceNormal(pixel, position);
  const float roughness = 1.f;
#endif
  normal_roughness[id.xy] = NRD_FrontEnd_PackNormalAndRoughness(normal, roughness, 0.f);
#if defined(NRD_FRONT_SUN)
  const float2 ray = sun_rays[id.xy];
  const float occluder = (ray.x >= LIT_FROM) ? NRD_FP16_MAX : ray.y;
  penumbra[id.xy] = SIGMA_FrontEnd_PackPenumbra(occluder, sun_cone);
  translucency[id.xy] = SIGMA_FrontEnd_PackTranslucency(occluder, ray.x);
#elif defined(NRD_FRONT_BOUNCE)
  signal[id.xy] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(bounce[id.xy].rgb, REBLUR_FrontEnd_GetNormHitDist(hit_distance[id.xy], position.w, HIT_DISTANCE_PARAMETERS, 1.f), true);
#elif defined(NRD_FRONT_AMBIENT)
  signal[id.xy] = ambient[id.xy];
#endif
}
