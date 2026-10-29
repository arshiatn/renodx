/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's local light shadows (bridge v9; dlaa_helper/rt_d3d12.hpp), a pass after the trace: from
// each render pixel's world position a ray toward each light within reach (bit i of light_mask: light i reaches the pixel
// unblocked; hard shadows, the rays stopping short of the light's own fixture). With more lights in reach than
// LIGHT_RAYS_ALWAYS + LIGHT_RAYS_TURNS (a burning city: each light every pixel reaches cost ~0.18 ms at 4K) the list's
// first are traced every frame and the others in turns; one not traced keeps the last trace's bit where it saw the same
// surface (light_history, in the ambient's parity). The harness at 4K: 16 lights in every pixel's reach 3.80 -> 3.13 ms,
// 32 lights 6.64 -> 5.17 ms, the bits the same as tracing all once the turns came round.
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_lights.h -Vn RT_LIGHTS; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_LIGHTS_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"

RWTexture2D<uint> light_mask : register(u22);
RWTexture2D<uint> light_history[2] : register(u32);  // the bits kept over frames

static const uint LIGHT_RAYS_ALWAYS = 4u;  // the list's first, traced every frame
static const uint LIGHT_RAYS_TURNS = 4u;   // and the others' a frame

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= width || id.y >= height) return;
  const float4 position = positions[id.xy];
  if (position.w <= 0.f) {
    light_mask[id.xy] = 0xFFFFFFFFu;
    return;
  }
  SceneRayFlags();
  // Lights out of reach or behind a planar surface keep their bit: their own shading leaves those pixels dark (at
  // corners, cracks and edges the normal from the depth can be wrong: traced).
  const float3 normal = SurfaceNormal(int2(id.xy), position);
  const bool planar = Planar(int2(id.xy), position, normal);
  const float3 light_origin = RayOrigin(position, normal, planar);
  uint reached = 0u;
  [loop]
  for (uint light = 0u; light < light_count; ++light) {
    const float3 to_light = lights[light].xyz - light_origin;
    const float light_distance = length(to_light);
    if (light_distance < lights[light].w && light_distance > 1e-3f && (!planar || dot(to_light, normal) > 0.f)) reached |= 1u << light;
  }
  uint traced = reached;
  uint lit = 0xFFFFFFFFu;
  uint2 seen_pixel;
  const uint reached_count = countbits(reached);
  if (reached_count > LIGHT_RAYS_ALWAYS + LIGHT_RAYS_TURNS && (path_flags & PATH_LIGHT_HISTORY) != 0u
      && SeenByLastTrace(position.xyz, seen_pixel)) {
    // The list's first LIGHT_RAYS_ALWAYS (the addon orders it by importance) every frame, the others in turns by their
    // place, each 8 x 8 block its own phase (a light's shadows change a block at a time); a light new to the list is
    // traced.
    const uint kept_bits = light_history[(path_flags & PATH_AMBIENT_PARITY) != 0u ? 1u : 0u][seen_pixel];
    const uint turns = (light_count - LIGHT_RAYS_ALWAYS + LIGHT_RAYS_TURNS - 1u) / LIGHT_RAYS_TURNS;
    const uint phase = frame + (id.x >> 3) + ((id.y >> 3) * 7u);
    uint chosen = 0u;
    uint kept_dark = 0u;  // the others the last trace found blocked
    [loop]
    for (uint light = 0u; light < light_count; ++light) {
      // Its byte (selected, not indexed: a vector indexed by a variable goes through local memory).
      const uint4 words = light_previous[light >> 4u];
      const uint lane = (light >> 2u) & 3u;
      const uint previous = (((lane == 0u) ? words.x : (lane == 1u) ? words.y : (lane == 2u) ? words.z : words.w) >> ((light & 3u) * 8u)) & 0xFFu;
      if (light < LIGHT_RAYS_ALWAYS || (light + phase) % turns == 0u || previous == 0xFFu) {
        chosen |= 1u << light;
      } else if (((kept_bits >> previous) & 1u) == 0u) {
        kept_dark |= 1u << light;
      }
    }
    traced = reached & chosen;
    lit = ~(reached & kept_dark);
  }
  // Soft shadows under Ray Reconstruction: each ray to a point of the light's disk (light_size), the bit lit where the
  // share of rays that reached it beats noise moved each frame, which RR averages into the share (hard: half). Several
  // rays a pixel (Ray Tracing at Output Resolution) each from its own spot of a planar pixel.
  const bool reconstructed = (flags & RECONSTRUCTED) != 0u;
  const bool soft = reconstructed && light_size > 0.f;
  const uint rays = clamp(shadow_rays, 1u, 4u);
  const float threshold = (reconstructed && (soft || rays > 1u)) ? InterleavedGradientNoise(float2(id.xy)) : 0.5f;
  const float turn = InterleavedGradientNoise(float2(id.xy) + 37.f);
  [loop]
  for (uint light = 0u; light < light_count; ++light) {
    if (((traced >> light) & 1u) == 0u) continue;
    uint reached_rays = 0u;
    [loop]
    for (uint ray = 0u; ray < rays; ++ray) {
      const float3 from = light_origin + ((rays > 1u && planar) ? PixelSpot(int2(id.xy), SpotOf(ray, turn)) : 0.f);
      float3 to_light = lights[light].xyz - from;
      if (soft) {
        const uint seed = (frame * 32u + light) * 16u + ray * 2u;
        to_light += DiskPoint(normalize(to_light), light_size, float2(Random(uint3(id.xy, seed)), Random(uint3(id.xy, seed + 1u))));
      }
      const float light_distance = length(to_light);
      if (!Occluded(from, to_light / light_distance, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS_SMOKE)) ++reached_rays;
    }
    if ((float)reached_rays <= threshold * (float)rays) lit &= ~(1u << light);
  }
  light_mask[id.xy] = lit;
  light_history[(path_flags & PATH_AMBIENT_PARITY) != 0u ? 0u : 1u][id.xy] = lit;
}
