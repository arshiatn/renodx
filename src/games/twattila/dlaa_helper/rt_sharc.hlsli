/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper's SHaRC radiance cache (SHaRC Radiance Cache, Experimental): NVIDIA's SHaRC 1.6.5 (NVIDIA RTX SDKs
// License; its headers from build\downloads\sharc at build time, build.ps1). This software contains source code provided
// by NVIDIA Corporation. A world hash grid of the light leaving surfaces (voxels a fortieth of their distance from the
// camera), filled by sparse paths (rt_sharc_update.hlsl), merged with the frames before it
// (rt_sharc_resolve.hlsl) and read where a Light Bounce path meets a surface the screen does not know
// (rt_path.hlsli PathRadiance) in place of the helper's own cache (the reflections keep it: their SHaRC variant hung the
// GPU at 4K). Its buffers sit at u6-u8 (root descriptors).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_

static const uint SHARC_CAPACITY = 1u << 21;  // rt_d3d12.hpp SHARC_ENTRIES
// Every access of its buffers within them (root descriptors: no bounds checks).
#define BUFFER_AT_OFFSET(name, offset) name[min((offset), SHARC_CAPACITY - 1u)]
#define SHARC_ENABLE_64_BIT_ATOMICS 1
#include "SharcCommon.h"

RWStructuredBuffer<uint64_t> sharc_keys : register(u6);
RWStructuredBuffer<SharcAccumulationData> sharc_accumulated : register(u7);
RWStructuredBuffer<SharcPackedData> sharc_resolved : register(u8);

static const float SHARC_SCENE_SCALE = 40.f;  // a voxel's size: its distance from the camera / 40 (25 cm at 10 m)
static const float SHARC_RADIANCE_SCALE = 1000.f;

SharcParameters Sharc() {
  SharcParameters parameters;
  parameters.gridParameters.cameraPosition = Camera();
  parameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
  parameters.gridParameters.sceneScale = SHARC_SCENE_SCALE;
  parameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;
  parameters.hashMapData.capacity = SHARC_CAPACITY;
  parameters.hashMapData.hashEntriesBuffer = sharc_keys;
  parameters.radianceScale = SHARC_RADIANCE_SCALE;
  parameters.enableAntiFireflyFilter = true;
  parameters.accumulationBuffer = sharc_accumulated;
  parameters.resolvedBuffer = sharc_resolved;
  return parameters;
}

// A voxel's size at `position`: a ray shorter than it reads no cache (that light is its own surface's).
float SharcVoxelSize(float3 position) {
  const HashGridParameters grid = Sharc().gridParameters;
  return HashGridGetVoxelSize(HashGridGetLevel(position, grid), grid);
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SHARC_HLSLI_
