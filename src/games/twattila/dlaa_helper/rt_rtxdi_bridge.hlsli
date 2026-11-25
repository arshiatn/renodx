/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// RTXDI's application bridge (RAB) for Torch Light in Path-Traced GI's ReGIR (PT DI step 3, T2; the PATH_REGIR builds:
// every includer of rt_path.hlsli and rt_regir_presample.hlsl). It calls NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX
// SDKs License; build.ps1 compiles its headers from the unmodified copy in build\downloads\rtxdi: not part of this file;
// the helper's notices, credits.hpp, carry NVIDIA's sentence). This software contains source code provided by NVIDIA
// Corporation.
// The local lights are b4's (rt_scene.hlsli Lights), a light the index of one: the ReGIR cells (an onion around the
// camera, b4's regir) hold RIS picks of them by what each could give the cell, chosen among uniform candidates
// (REGIR_LOCAL_LIGHT_PRESAMPLING_MODE_UNIFORM: at most 128 lights, the cells do the locality; no light PDF texture). A
// surface the paths meet resamples a few of its cell's picks by what they give it (rt_path.hlsli TorchLight).
// InitialSampling.hlsli holds ReGIR's cell pick; dxc checks every body it holds, so its RAB functions this bridge does not
// use are here as stubs (DI's initial sampling, step 4c, gives them their meaning).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_RTXDI_BRIDGE_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_RTXDI_BRIDGE_HLSLI_

// The cells' picks, (light, its RIS weight) a slot, lights-per-cell slots a cell (rt_d3d12.hpp REGIR_CELLS).
RWBuffer<uint2> regir_cells : register(u76);
#define RTXDI_RIS_BUFFER regir_cells

struct RAB_Surface {
  float3 origin;
  float3 normal;
};
struct RAB_LightInfo {
  uint index;
};
struct RAB_LightSample {
  float3 position;
};

#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/DI/ReSTIRDIParameters.h"

RAB_LightInfo RAB_LoadLightInfo(uint index, bool previous) { return (RAB_LightInfo)index; }
RAB_LightInfo RAB_EmptyLightInfo() { return (RAB_LightInfo)0; }
// No compact copies in the cells: a light is its index.
bool RAB_StoreCompactLightInfo(uint slot, RAB_LightInfo light) { return false; }
RAB_LightInfo RAB_LoadCompactLightInfo(uint slot) { return (RAB_LightInfo)0; }
float3 RAB_GetSurfaceWorldPos(RAB_Surface surface) { return surface.origin; }

// What a light could give anything within `radius` of `center`: its colour's luminance at its falloff (LightLinear's
// kinds) from the sphere's nearest point, no angle; 0 out of reach. Never below what it gives a surface there (its
// LightArriving, but the diffuse scale every light shares): no light that lights a cell's surface is left out of it.
float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 center, float radius) {
  const float nearest = max(distance(lights[light.index].xyz, center) - radius, 0.f);
  if (nearest >= lights[light.index].w) return 0.f;
  const float falloff = 1.f - nearest / lights[light.index].w;
  return Luminance(max(light_colours[light.index].rgb, 0.f)) * (LightLinear(light.index) ? falloff : falloff * falloff);
}

// InitialSampling.hlsli's others (not called here).
RAB_LightSample RAB_EmptyLightSample() { return (RAB_LightSample)0; }
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo light, RAB_Surface surface, float2 uv) { return RAB_EmptyLightSample(); }
float RAB_LightSampleSolidAnglePdf(RAB_LightSample light_sample) { return 0.f; }
bool RAB_IsAnalyticLightSample(RAB_LightSample light_sample) { return true; }
void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample light_sample, out float3 direction, out float light_distance) {
  direction = 0.f;
  light_distance = 0.f;
}
float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 direction) { return 0.f; }
bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 direction) {
  direction = 0.f;
  return false;
}
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample light_sample, RAB_Surface surface) { return 0.f; }
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float t_min, float t_max, out uint light_index, out float2 uv) {
  light_index = RTXDI_InvalidLightIndex;
  uv = 0.f;
  return false;
}
float RAB_EvaluateLocalLightSourcePdf(uint light_index) { return 0.f; }
float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction) { return 0.f; }
float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction) { return 0.f; }
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample light_sample) { return true; }

#include "Rtxdi/DI/InitialSampling.hlsli"

// An RTXDI sampler seeded by H0's hash (rt_scene.hlsli Random's lanes: pixel or slot, frame, dimension), not
// RTXDI_InitRandomSampler's linear frame + pass seed; RTXDI's own steps inside one sampler.
RTXDI_RandomSamplerState RabSampler(uint4 lanes) {
  return RTXDI_CreateRandomSamplerFromDirectSeed(Pcg4d(lanes).y, 0u);
}

// b4's lights as RTXDI's local-light region (the presample's candidates, a surface outside the onion's picks).
RTXDI_LightBufferRegion RegirLights() {
  RTXDI_LightBufferRegion region = (RTXDI_LightBufferRegion)0;
  region.numLights = min(light_count, 128u);
  return region;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_RTXDI_BRIDGE_HLSLI_
