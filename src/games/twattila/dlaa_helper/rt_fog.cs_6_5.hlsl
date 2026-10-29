/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray-traced fog shadows (dlaa_helper/rt_d3d12.hpp, bridge v11): per fog texel (a quarter of the render
// pixels each way) the sun's visibility along the camera's ray to the texel's surface: FOG_SAMPLES points, closer
// together near the camera (where shafts show; each weighted by the stretch it stands for), each one ray toward the
// sun (dimmed by the smoke it meets). The games' fog pass (twattila/rt_fog.hlsli) dims the sun's share of its colour by
// it: light shafts through gaps, the fog in shadow darker. Sky texels and frames without a sun: 1. Noise moves every
// frame (DLAA / DLSS settle it). With FOG_LIGHTS (v18) the local lights' light at the same points into fog_lights (rgb):
// torches and fires glowing in the fog, their shafts and shadows too (rt_fog.hlsli adds it to the fog's colour).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_fog.h -Vn RT_FOG; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_FOG_OMM.
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> fog : register(u27);
RWTexture2D<float4> fog_lights : register(u34);

// The local lights' light at a point of the fog: one light picked by what it gives there (its colour's brightness times
// the games' smooth falloff (1 - distance / reach)^2; reservoir sampling with one number), weighted back by its odds,
// through a shadow ray that the smoke it meets dims.
float3 FogLightAt(float3 at, float u) {
  float total = 0.f;
  uint picked = 32u;
  float picked_weight = 0.f;
  [loop]
  for (uint light = 0u; light < min(light_count, 32u); ++light) {
    const float falloff = saturate(1.f - (distance(lights[light].xyz, at) / max(lights[light].w, 1e-3f)));
    const float weight = dot(light_colours[light].rgb, float3(0.2126f, 0.7152f, 0.0722f)) * falloff * falloff;
    if (weight <= 0.f) continue;
    total += weight;
    const float keep = weight / total;
    if (u < keep) {
      picked = light;
      picked_weight = weight;
      u /= keep;
    } else {
      u = (u - keep) / (1.f - keep);
    }
  }
  if (picked == 32u) return 0.f;
  const float3 to_light = lights[picked].xyz - at;
  const float light_distance = length(to_light);
  smoke_transmittance = 1.f;
  if (light_distance <= 1e-3f
      || Occluded(at, to_light / light_distance, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS)) {
    return 0.f;
  }
  const float falloff = saturate(1.f - (light_distance / max(lights[picked].w, 1e-3f)));
  return light_colours[picked].rgb * (falloff * falloff * total / picked_weight * smoke_transmittance);
}

static const uint FOG_SAMPLES = 8u;
static const float FOG_RANGE = 400.f;      // metres along the camera's ray at most
static const float FOG_SUN_RANGE = 1000.f;  // metres toward the sun

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= (width + 3u) / 4u || id.y >= (height + 3u) / 4u) return;
  const float4 position = positions[min(id.xy * 4u + 2u, uint2(width, height) - 1u)];
  const float4 toward_sun = sun[uint2(0, 0)];
  const bool glowing = (flags & FOG_LIGHTS) != 0u && light_count != 0u && position.w > 0.f;
  if (!glowing && (flags & FOG_LIGHTS) != 0u) fog_lights[id.xy] = 0.f;
  if (position.w <= 0.f || (toward_sun.w == 0.f && !glowing)) {
    fog[id.xy] = 1.f;
    return;
  }
  const float3 camera = sun[uint2(1, 0)].xyz;
  const float surface_distance = distance(position.xyz, camera);
  const float3 direction = (position.xyz - camera) / max(surface_distance, 1e-3f);
  const float range = min(surface_distance, FOG_RANGE);
  const float jitter = Random(uint3(id.xy, frame));
  float lit = 0.f;
  float3 glow = 0.f;
  float weights = 0.f;
  [loop]
  for (uint index = 0u; index < FOG_SAMPLES; ++index) {
    // Squared spacing: a point at `along` stands for a stretch in proportion to it.
    const float along = (index + jitter) / FOG_SAMPLES;
    const float3 at = camera + direction * (along * along * range);
    smoke_transmittance = 1.f;
    if (toward_sun.w != 0.f && !Occluded(at, toward_sun.xyz, FOG_SUN_RANGE, NO_GRASS)) lit += along * smoke_transmittance;
    if (glowing) glow += along * FogLightAt(at, Random(uint3(id.xy, frame * 16u + index)));
    weights += along;
  }
  fog[id.xy] = (weights > 0.f && toward_sun.w != 0.f) ? lit / weights : 1.f;
  if (glowing) fog_lights[id.xy] = float4((weights > 0.f) ? glow / weights : 0.f, 0.f);
}
