/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SHaRC Radiance Cache's update (rt_sharc.hlsli; this software contains source code provided by NVIDIA Corporation):
// one bounce texel of each SHARC_BLOCK x SHARC_BLOCK block (another each frame) traces a path of up to SHARC_BOUNCES
// rays. Each surface it meets adds its light to its voxel: where the screen knows the surface its light (the path ends
// there), else the sun's and the local lights' (as rt_path's unseen surfaces) and the path goes on (or SHaRC ends it on
// the cache's own light); a ray that escapes brings the sky's. SHaRC carries each voxel's light back to the surface before
// it (SHARC_PROPAGATION_DEPTH 2: SHaRC's 4 with its cache resampling hung the GPU in the harness, the 5090; 2 or no
// resampling did not), the cache's resampling the bounces past it over frames.
// Build (build.ps1, with SHaRC): dxc -T cs_6_9 -D OPACITY_MICROMAPS -D SHARC -D SHARC_UPDATE=1
// -D SHARC_PROPAGATION_DEPTH=2 -enable-16bit-types -Fh rt_sharc_update.h -Vn RT_SHARC_UPDATE.
#include "./rt_path.hlsli"

static const uint SHARC_BLOCK = 5u;   // rt_d3d12.hpp SHARC_BLOCK: 4% of the texels a frame (SHaRC's guide)
static const uint SHARC_BOUNCES = 4u;

[numthreads(8, 8, 1)]
void main(uint3 block : SV_DispatchThreadID) {
  const uint2 pick = min(uint2(float2(Random(uint3(block.xy, frame * 16u + 40000u)), Random(uint3(block.xy, frame * 16u + 40001u))) * SHARC_BLOCK),
                         SHARC_BLOCK - 1u);
  const uint2 id = block.xy * SHARC_BLOCK + pick;
  if (id.x >= bounce_width || id.y >= bounce_height || light_kept[0].w <= 0.f) return;
  float3 normal;
  float3 origin;
  if (TexelSurface(id, normal, origin).w <= 0.f) return;
  const SharcParameters sharc = Sharc();
  SharcState state;
  SharcInit(state);
  float3 direction = CosineDirection(normal, Random(uint3(id, frame * 16u + 40002u)), Random(uint3(id, frame * 16u + 40003u)));
  [loop]
  for (uint vertex = 0u; vertex < SHARC_BOUNCES; ++vertex) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = bounce_range;
    TRACE_SURFACE(query, ray)
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
      SharcUpdateMiss(sharc, state, SkyLight(direction, light_kept[0].rgb));
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    const uint record_index = query.CommittedInstanceID() + query.CommittedGeometryIndex();
    const float3 hit_normal = HitNormal(record_index, query.CommittedPrimitiveIndex(), direction, query.CommittedObjectToWorld3x4());
    const SharcHitData sharc_hit = {position, hit_normal};
    const uint light_seed = frame * 64u + vertex * 2u + 41000u;
    float3 known;
    if (HitLight(position, known)) {
      SharcUpdateHit(sharc, state, sharc_hit, known, Random(uint3(id, light_seed + 1u)));
      break;
    }
    const float3 albedo = HitAlbedo(position);
    const float offset = SurfaceOffset(distance(position, Camera()));
    const uint record_flags = records[record_index].z >> 16;
    const float transmittance = ((record_flags & (RECORD_MASKED | RECORD_GRASS)) == RECORD_MASKED) ? LEAF_TRANSMITTANCE : (((record_flags & RECORD_THIN) != 0u) ? THIN_TRANSMITTANCE : 0.f);
    const float3 surface = position + hit_normal * offset;
    const float3 direct = UnseenLight(surface, position - hit_normal * offset, hit_normal, albedo, transmittance, id, light_seed);
    if (!SharcUpdateHit(sharc, state, sharc_hit, direct, Random(uint3(id, light_seed + 1u)))) break;
    SharcSetThroughput(state, albedo);
    origin = surface;
    direction = CosineDirection(hit_normal, Random(uint3(id, light_seed + 2u)), Random(uint3(id, light_seed + 3u)));
  }
}
