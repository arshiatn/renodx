/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smoke shadows without rays (smoke_map.hpp): the particles streamed out (rt_core.hpp CaptureParticles; a draw holds its
// whole list, nearly all without opacity) that show into the splats the map draws, their count in the draw's arguments.
// A splat's optical depth is spread by Softness: radii squared add and the middle falls so that the smoke's whole depth
// stays (blurring it, without a blur pass).
#include "./smoke_map.hlsli"

Buffer<float4> particles : register(t0);  // three a particle: centre, colour and opacity, size (xy) and emitter (w)
StructuredBuffer<SmokeView> view : register(t1);
RWStructuredBuffer<SmokeSplat> splats : register(u0);  // past its end: writes dropped, the extra instances read radius 0
RWByteAddressBuffer arguments : register(u1);

cbuffer cb_vfx_emitter_constants_common : register(b3)
{
  uint4 behaviour_mask[512];
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  const SmokeView map = view[0];
  if (id.x >= g_smoke_particles || map.coverage == 0.f) return;
  const float3 centre = particles[id.x * 3u].xyz;
  const float opacity = min(saturate(particles[id.x * 3u + 1u].w), 0.98f);
  const float4 size = particles[id.x * 3u + 2u];
  const float radius = 0.5f * sqrt(max(size.x * size.y, 0.f));
  // As rt_smoke: less than 0.05 m^2 of full opacity (arrows' trails) shows no shadow.
  if (!(opacity * radius * radius * 4.f >= 0.05f) || !all(isfinite(centre))) return;
  // The textures' weather (vfx00): snow (slice 1) and the fog and haze kept around the camera (slice 7) cast none.
  const uint4 behaviour = behaviour_mask[min((uint)size.w, 511u)];
  if ((behaviour.x & 0x20000u) != 0u && (behaviour.y == 1u || behaviour.y == 7u)) return;
  const float2 at = float2(dot(centre, map.right), dot(centre, map.up)) - map.origin;
  const float half_coverage = map.coverage * 0.5f;
  const float texel = map.coverage / SMOKE_MAP_SIZE;
  // rt_scene.hlsli SmokeDensity's disk has the area of this kernel at 1.3 x its radius. Two texels at least: no aliasing.
  const float disk_squared = 1.69f * radius * radius;
  const float spread_squared = disk_squared + (g_smoke_softness * g_smoke_softness) + (4.f * texel * texel);
  const float spread = sqrt(spread_squared);
  if (any(abs(at) >= half_coverage + spread)) return;
  uint index = 0u;
  arguments.InterlockedAdd(4u, 1u, index);
  // The moments of the smoke's height (smoke_map_apply reads how much of it lies above a pixel from them).
  const float height = (centre.y - map.height) / SMOKE_MAP_HEIGHT_SCALE;
  const float extent = radius / SMOKE_MAP_HEIGHT_SCALE;
  SmokeSplat splat = (SmokeSplat)0;
  splat.centre = at / half_coverage;
  splat.radius = spread / half_coverage;
  splat.optical = -log(1.f - opacity) * (disk_squared / spread_squared) * float4(1.f, height, (height * height) + (extent * extent / 3.f), 0.f);
  splats[index] = splat;
}
