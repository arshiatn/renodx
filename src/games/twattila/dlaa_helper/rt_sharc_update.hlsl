/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// SHaRC Radiance Cache's update (rt_sharc.hlsli: compiled with NVIDIA's SHaRC 1.8.3 headers, not part of this file):
// one bounce texel of each SHARC_BLOCK x SHARC_BLOCK block (another each frame) traces a path of up to SHARC_BOUNCES
// rays. Each surface it meets adds its light to its voxel: where the screen knows the surface its light (the path ends
// there), else the sun's and the local lights' (as rt_path's unseen surfaces) and the path goes on (or SHaRC ends it on
// the cache's own light); a ray that escapes brings the sky's (PathRadiance's: SkyLight of KeptShade, calibrated at the
// surface it left). It runs once the sun's light is known (light_kept[1], as PathRadiance's cache adds: with the game's
// sun light the learned shade light_kept[0] is never solved). SHaRC carries each voxel's light back to the surface before
// it (SHARC_PROPAGATION_DEPTH 2, 1.8.3's default with resampling: 4 with resampling hung the GPU in the harness, the
// 5090, when the cache was new every frame; never 4), the cache's resampling the bounces past it over frames. Every light
// handed in through SharcInput (finite, capped). SHaRC stops a path whose voxel it cannot insert (a full bucket).
// Build (build.ps1, with SHaRC): dxc -T cs_6_9 -D OPACITY_MICROMAPS -D SHARC -D SHARC_UPDATE=1
// -D SHARC_PROPAGATION_DEPTH=2 -enable-16bit-types -Fh rt_sharc_update.h -Vn RT_SHARC_UPDATE.
#include "./rt_path.hlsli"

static const uint SHARC_BLOCK = 5u;   // rt_d3d12.hpp SHARC_BLOCK: 4% of the texels a frame (SHaRC's guide)
static const uint SHARC_BOUNCES = 4u;

[numthreads(8, 8, 1)]
void main(uint3 block : SV_DispatchThreadID) {
  const uint2 pick = min(uint2(float2(Random(uint4(block.xy, frame, DIM_SHARC_PICK)), Random(uint4(block.xy, frame, DIM_SHARC_PICK + 1u))) * SHARC_BLOCK),
                         SHARC_BLOCK - 1u);
  const uint2 id = block.xy * SHARC_BLOCK + pick;
  const float4 shade = float4(KeptShade(), light_kept[1].w);  // w: the sun's light known
  if (id.x >= bounce_width || id.y >= bounce_height || shade.w <= 0.f) return;
  float3 normal;
  float3 origin;
  if (TexelSurface(id, normal, origin).w <= 0.f) return;
  const SharcParameters sharc = Sharc();
  SharcState state;
  SharcInit(state);
  float3 direction = CosineDirection(normal, Random(uint4(id, frame, DIM_SHARC_FIRST)), Random(uint4(id, frame, DIM_SHARC_FIRST + 1u)));
  float3 leaving_normal = normal;
  [loop]
  for (uint vertex = 0u; vertex < SHARC_BOUNCES; ++vertex) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = bounce_range;
    TRACE_SURFACE(query, ray)
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
      SharcUpdateMiss(sharc, state, SharcInput(SkyLight(direction, shade.rgb) * SkyCalibration(leaving_normal)));
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    const uint record_index = query.CommittedInstanceID() + query.CommittedGeometryIndex();
    const float3 hit_normal = HitNormal(record_index, query.CommittedPrimitiveIndex(), direction, query.CommittedObjectToWorld3x4());
    const SharcHitData sharc_hit = {position, hit_normal};
    float3 known;
    if (HitLight(position, hit_normal, known)) {
      SharcUpdateHit(sharc, state, sharc_hit, SharcInput(known), Random(uint4(id, frame, DIM_SHARC_HIT + vertex * 2u)));
      break;
    }
    const float3 albedo = HitAlbedo(position, hit_normal);
    const float offset = SurfaceOffset(distance(position, Camera()));
    const uint record_flags = records[record_index].z >> 16;
    const float transmittance = ((record_flags & (RECORD_MASKED | RECORD_GRASS)) == RECORD_MASKED) ? LEAF_TRANSMITTANCE : (((record_flags & RECORD_THIN) != 0u) ? THIN_TRANSMITTANCE : 0.f);
    const float3 surface = position + hit_normal * offset;
    const float3 direct = UnseenLight(surface, position - hit_normal * offset, hit_normal, albedo, transmittance,
                                       uint4(id, frame, DIM_SHARC_LIGHTS + vertex * 2u));
    if (!SharcUpdateHit(sharc, state, sharc_hit, SharcInput(direct), Random(uint4(id, frame, DIM_SHARC_HIT + vertex * 2u)))) break;
    SharcSetThroughput(state, albedo);
    origin = surface;
    leaving_normal = hit_normal;
    direction = CosineDirection(hit_normal, Random(uint4(id, frame, DIM_SHARC_DIRECTION + vertex * 2u)), Random(uint4(id, frame, DIM_SHARC_DIRECTION + vertex * 2u + 1u)));
  }
}
