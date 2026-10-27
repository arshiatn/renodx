/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray-traced fog shadows (dlaa_helper/rt_d3d12.hpp, bridge v11): per fog texel (a quarter of the render
// pixels each way) the sun's visibility along the camera's ray to the texel's surface: FOG_SAMPLES points, closer
// together near the camera (where shafts show; each weighted by the stretch it stands for), each one ray toward the
// sun. The games' fog pass (twattila/rt_fog.hlsli) dims the sun's share of its colour by it: light shafts through gaps,
// the fog in shadow darker. Sky texels and frames without a sun: 1. Noise moves every frame (DLAA / DLSS settle it).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_fog.h -Vn RT_FOG; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_FOG_OMM.
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> fog : register(u27);

static const uint FOG_SAMPLES = 8u;
static const float FOG_RANGE = 400.f;      // metres along the camera's ray at most
static const float FOG_SUN_RANGE = 1000.f;  // metres toward the sun

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= (width + 3u) / 4u || id.y >= (height + 3u) / 4u) return;
  const float4 position = positions[min(id.xy * 4u + 2u, uint2(width, height) - 1u)];
  const float4 toward_sun = sun[uint2(0, 0)];
  if (position.w <= 0.f || toward_sun.w == 0.f) {
    fog[id.xy] = 1.f;
    return;
  }
  const float3 camera = sun[uint2(1, 0)].xyz;
  const float surface_distance = distance(position.xyz, camera);
  const float3 direction = (position.xyz - camera) / max(surface_distance, 1e-3f);
  const float range = min(surface_distance, FOG_RANGE);
  const float jitter = Random(uint3(id.xy, frame));
  float lit = 0.f;
  float weights = 0.f;
  [loop]
  for (uint index = 0u; index < FOG_SAMPLES; ++index) {
    // Squared spacing: a point at `along` stands for a stretch in proportion to it.
    const float along = (index + jitter) / FOG_SAMPLES;
    if (!Occluded(camera + direction * (along * along * range), toward_sun.xyz, FOG_SUN_RANGE)) lit += along;
    weights += along;
  }
  fog[id.xy] = (weights > 0.f) ? lit / weights : 1.f;
}
