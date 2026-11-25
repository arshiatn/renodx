/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's first pass (PT DI step 4c; a trace that ran rt_lights' LIGHTS_DI build): each DI pixel's
// initial reservoir (rt_di_initial.hlsli) into DI_INITIAL_ARRAY. NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License;
// rt_rtxdi_bridge.hlsli). This software contains source code provided by NVIDIA Corporation.
// Build (build.ps1, RTXDI builds): dxc -T cs_6_5 -D PATH_DI -D PATH_REGIR -I <RTXDI>\Include -Fh rt_di_initial.h
// -Vn RT_DI_INITIAL; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_DI_INITIAL_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"
#include "./rt_light_tiles.hlsli"
#include "./rt_di_surface.hlsli"
#include "./rt_rtxdi_bridge.hlsli"
#include "./rt_di_initial.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  const RAB_Surface surface = DiCentreSurface(id.xy);
  if (!RAB_IsSurfaceValid(surface)) return;
  RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_DI_INITIAL));
  RAB_LightSample picked;
  RTXDI_StoreDIReservoir(DiInitialSample(id.xy, surface, rng, picked), DiReservoirParameters(), id.xy, DI_INITIAL_ARRAY);
}
