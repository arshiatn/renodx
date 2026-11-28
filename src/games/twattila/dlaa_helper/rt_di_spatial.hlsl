/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's spatial pass (PT DI step 4c): each DI pixel's temporal reservoir merged with two DI
// neighbours' within 32 pixels (RTXDI's DISpatialResampling: stratified offsets, eight on a short history, the same
// light model and gloss; ray-traced bias correction from each neighbour), into DI_SHADE_ARRAY (the shading's, and the
// next trace's history). NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License; rt_rtxdi_bridge.hlsli). This software
// contains source code provided by NVIDIA Corporation.
// Build (build.ps1, RTXDI builds): dxc -T cs_6_5 -D PATH_DI -D PATH_REGIR -I <RTXDI>\Include -Fh rt_di_spatial.h
// -Vn RT_DI_SPATIAL; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_DI_SPATIAL_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"
#include "./rt_light_tiles.hlsli"
#include "./rt_di_surface.hlsli"
#include "./rt_rtxdi_bridge.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"

// src: spec:rtxdi@fdbd9b3:Samples/FullSample/Shaders/LightingPasses/DI/SpatialResampling.hlsl:22 the spatial pass: the
// temporal output resampled with its neighbours, stored.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  const RAB_Surface surface = RAB_GetGBufferSurface(int2(id.xy), false);
  // dev: spec:rtxdi@fdbd9b3:Samples/FullSample/Shaders/LightingPasses/DI/SpatialResampling.hlsl:42: no store at a
  // non-DI pixel (RTXDI stores an empty reservoir there); RAB_GetGBufferSurface rejects that pixel as a neighbour or
  // history, so its old reservoir is never read.
  if (!RAB_IsSurfaceValid(surface)) return;
  const RTXDI_ReservoirBufferParameters reservoirs = DiReservoirParameters();
  RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(reservoirs, id.xy, DI_TEMPORAL_ARRAY);
  if ((di_flags & DI_NO_SPATIAL) == 0u) {
    RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_DI_SPATIAL));
    RAB_LightSample picked = RAB_EmptyLightSample();
    reservoir = RTXDI_DISpatialResampling(id.xy, surface, reservoir, rng, DiRuntimeParameters(), reservoirs, DI_TEMPORAL_ARRAY, DiSpatialParameters(),
                                          picked);
  }
  RTXDI_StoreDIReservoir(reservoir, reservoirs, id.xy, DI_SHADE_ARRAY);
}
