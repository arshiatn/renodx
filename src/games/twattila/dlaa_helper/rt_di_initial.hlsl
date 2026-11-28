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

// src: spec:rtxdi@fdbd9b3:Samples/FullSample/Shaders/LightingPasses/DI/GenerateInitialSamples.hlsl:22 the initial pass:
// the pixel's surface, its sampled reservoir, stored.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  RAB_Surface surface = RAB_GetGBufferSurface(int2(id.xy), false);
  // dev: spec:rtxdi@fdbd9b3:Samples/MinimalSample/Shaders/InitialSampling.hlsl:34: no store at a non-DI pixel (RTXDI
  // stores an empty reservoir there); RAB_GetGBufferSurface rejects that pixel as a neighbour or history, so its old
  // reservoir is never read.
  if (!RAB_IsSurfaceValid(surface)) return;
  // The pixel's rays start where rt_lights' do (off a planar surface along the normal from the depth, else toward the
  // camera; past a hidden shape with Rays Start Past Hidden Shapes), stored for every DI pass that loads this surface,
  // as its own or as a neighbour, this trace or the next (B1). Ours: kept as a half-float offset (8 bytes a pixel; at
  // most ~0.7 mm off for StartPast's 1.5 m, under SurfaceOffset's 3 mm) and used rounded here too, so every pass's
  // origin is the same float.
  const float4 position = float4(surface.position, surface.depth);
  const float3 normal = SurfaceNormal(int2(id.xy), position);
  const float3 offset = f16tof32(f32tof16(StartPast(RayOrigin(position, normal, Planar(int2(id.xy), position, normal)), position) - position.xyz));
  di_origin[DiParity()][id.xy] = float4(offset, 0.f);
  surface.origin = position.xyz + offset;
  RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_DI_INITIAL));
  RAB_LightSample picked;
  RTXDI_StoreDIReservoir(DiInitialSample(id.xy, surface, rng, picked), DiReservoirParameters(), id.xy, DI_INITIAL_ARRAY);
}
