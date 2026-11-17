/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's SHaRC radiance cache (SHaRC Radiance Cache, Experimental), compiled with NVIDIA's SHaRC 1.8.3 headers
// (NVIDIA RTX SDKs License; build.ps1 fetches them into build\downloads\sharc at build time: not part of this file; the
// helper's notices, credits.hpp, carry NVIDIA's sentence). A world hash grid of the light leaving surfaces (voxels a
// fortieth of their distance from the camera), filled by sparse paths (rt_sharc_update.hlsl), merged with the frames
// before it (rt_sharc_resolve.hlsl) and read where a Light Bounce path meets a surface the screen does not know
// (rt_path.hlsli PathRadiance) in place of the helper's own cache (the reflections keep it: their SHaRC variant hung the
// GPU at 4K). Its buffers sit at u6-u8 (root descriptors).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_

static const uint SHARC_CAPACITY = 1u << 21;  // rt_d3d12.hpp SHARC_ENTRIES
// Every access of its buffers within them (root descriptors: no bounds checks).
#define BUFFER_AT_OFFSET(name, offset) name[min((offset), SHARC_CAPACITY - 1u)]
#define HASH_GRID_ENABLE_64_BIT_ATOMICS 1
#include "SharcCommon.h"

RWStructuredBuffer<uint64_t> sharc_keys : register(u6);
RWStructuredBuffer<SharcAccumulationData> sharc_accumulated : register(u7);
RWStructuredBuffer<SharcPackedData> sharc_resolved : register(u8);

static const float SHARC_SCENE_SCALE = 40.f;  // a voxel's size: its distance from the camera / 40 (25 cm at 10 m)
static const float SHARC_RADIANCE_SCALE = 1000.f;
// A sample's largest luminance: the update's sums are uint32 at SHARC_RADIANCE_SCALE (4.29e6 a channel a voxel a frame),
// so a voxel holds over 4,000 such samples a frame before one wraps.
static const float SHARC_SAMPLE_MAX = 1024.f;

SharcParameters Sharc() {
  SharcParameters parameters;
  parameters.hashGridParameters.cameraPosition = Camera();
  parameters.hashGridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
  parameters.hashGridParameters.sceneScale = SHARC_SCENE_SCALE;
  parameters.hashGridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;
  parameters.hashGridData.capacity = SHARC_CAPACITY;
  parameters.hashGridData.hashEntriesBuffer = sharc_keys;
  parameters.radianceScale = SHARC_RADIANCE_SCALE;
  parameters.accumulationBuffer = sharc_accumulated;
  parameters.resolvedBuffer = sharc_resolved;
  return parameters;
}

// Light handed to the update: NaN and negative channels as 0 (max takes the number; a NaN's uint conversion is undefined),
// infinite ones at FP16's most, its luminance at most SHARC_SAMPLE_MAX (1.8.3 has no anti-firefly filter; 1.6.5's acted
// only on sample weights above 2, ours are at most 1).
float3 SharcInput(float3 light) {
  const float3 finite = min(max(light, 0.f), 65504.f);
  return finite * min(SHARC_SAMPLE_MAX / max(Luminance(finite), 1e-30f), 1.f);
}

// A voxel's size at `position`: a ray shorter than its diagonal reads no cache (that light may be its own surface's;
// SHaRC 1.8.3's validSegmentLength).
float SharcVoxelSize(float3 position) {
  const HashGridParameters grid = Sharc().hashGridParameters;
  return HashGridGetVoxelSize(HashGridGetLevel(position, grid), grid);
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_
