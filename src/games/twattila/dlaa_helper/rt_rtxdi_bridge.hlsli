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
#if defined(PATH_DI)
  // Path-Traced Torch Light (PT DI step 4c, rt_di_surface.hlsli): a DI pixel's (normal: the game's, origin: its rays'
  // start) world position, view depth (0: no DI surface), light surface texel and its tile's listed lights.
  float3 position;
  float depth;
  uint4 texel;
  uint4 members;
#endif
};
struct RAB_LightInfo {
  uint index;
};
struct RAB_LightSample {
  float3 position;
#if defined(PATH_DI)
  uint light;  // its light (RTXDI_InvalidLightIndex: none)
#endif
};

#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/DI/ReSTIRDIParameters.h"

#if defined(PATH_DI)
// The last trace's light as this trace's (rt_di_surface.hlsli DiCurrentLight: the temporal pass; the same position and
// reach, this trace's colour).
RAB_LightInfo RAB_LoadLightInfo(uint index, bool previous) { return (RAB_LightInfo)(previous ? DiCurrentLight(index) : index); }
#else
RAB_LightInfo RAB_LoadLightInfo(uint index, bool previous) { return (RAB_LightInfo)index; }
#endif
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
#if defined(PATH_DI)
RAB_LightSample RAB_EmptyLightSample() {
  RAB_LightSample light_sample = (RAB_LightSample)0;
  light_sample.light = RTXDI_InvalidLightIndex;
  return light_sample;
}
// A point of the light's disk (DiSamplePoint) seen from the surface's ray origin.
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo light, RAB_Surface surface, float2 uv) {
  RAB_LightSample light_sample = RAB_EmptyLightSample();
  if (light.index < min(light_count, 128u)) {
    light_sample.position = DiSamplePoint(light.index, surface.origin, uv);
    light_sample.light = light.index;
  }
  return light_sample;
}
#else
RAB_LightSample RAB_EmptyLightSample() { return (RAB_LightSample)0; }
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo light, RAB_Surface surface, float2 uv) { return RAB_EmptyLightSample(); }
#endif
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
#if defined(PATH_DI)
// The light's game light at the surface where its tile lists it (DiTarget).
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample light_sample, RAB_Surface surface) {
  return DiTarget(surface.texel, surface.position, surface.members, light_sample.light);
}
#else
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample light_sample, RAB_Surface surface) { return 0.f; }
#endif
bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float t_min, float t_max, out uint light_index, out float2 uv) {
  light_index = RTXDI_InvalidLightIndex;
  uv = 0.f;
  return false;
}
float RAB_EvaluateLocalLightSourcePdf(uint light_index) { return 0.f; }
float2 RAB_GetEnvironmentMapRandXYFromDir(float3 direction) { return 0.f; }
float RAB_EvaluateEnvironmentMapSamplingPdf(float3 direction) { return 0.f; }
#if defined(PATH_DI)
// A ray from the surface to the sample's point of the light (DiVisible: alpha-tested, grass and smoke passed).
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample light_sample) {
  return DiVisible(surface.origin, light_sample.position);
}
#else
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample light_sample) { return true; }
#endif

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

#if defined(PATH_DI)
// Every bias correction for both passes (RTXDI defaults it only in SpatialResampling.hlsli: without it the temporal pass
// compiles its 1/M normalization alone, dark where the last trace's reservoir could not produce this pixel's lights).
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
// Path-Traced Torch Light's RAB (PT DI step 4c; the DI passes, rt_di_surface.hlsli before this file): RTXDI's DI
// reservoirs (three arrays, rt_di_surface.hlsli DI_*_ARRAY), its spatial neighbours' offsets (RTXDI's
// FillNeighborOffsetBuffer, stratified, as floats) and the two lights the shading writes (rt_di_shade).
RWStructuredBuffer<RTXDI_PackedDIReservoir> di_reservoirs : register(u80);
#define RTXDI_LIGHT_RESERVOIR_BUFFER di_reservoirs
RWBuffer<float2> di_offsets : register(u81);
#define RTXDI_DI_NEIGHBOR_OFFSETS_BUFFER di_offsets
RWTexture2D<float4> di_light[2] : register(u82);  // the light SSAO dims (slm), the light it does not (skinlm)

// The DI pixel's surface at `pixel` (this trace's, or the last's: its position and light surface; this trace's camera
// and tile members, a stated bias), else an empty one: off the screen, sky, no point-light model, or not a DI pixel in
// that trace (its light history's word not DI_PIXEL_WORD: its reservoirs were not written). Its rays start off it
// toward the camera, whose way is clear (DiCentreSurface: the pixel's own rays as rt_lights').
RAB_Surface RAB_GetGBufferSurface(int2 pixel, bool previous) {
  RAB_Surface surface = (RAB_Surface)0;
  // (No DI surface without the game's HDR state: an exact pixel's history word can be DI_PIXEL_WORD then.)
  if (any(pixel < 0) || pixel.x >= (int)width || pixel.y >= (int)height || !DiShadedFrame()) return surface;
  const uint2 at = uint2(pixel);
  const float4 position = previous ? LastTracePosition(at) : positions[at];
  if (!(position.w > 0.f) || light_history[previous ? 1u - DiParity() : DiParity()][at] != DI_PIXEL_WORD) return surface;
  const uint4 texel = previous ? light_surface_previous[at] : light_surface[at];
  const uint model = texel.w & 0xFFu;
  const float3 normal = float3(texel.x & 0xFFu, (texel.x >> 8u) & 0xFFu, (texel.x >> 16u) & 0xFFu) * (2.f / 255.f) - 1.f;
  if ((model != TW_POINT_LIGHT_SLM && model != TW_POINT_LIGHT_SKIN) || !(dot(normal, normal) > 1e-6f)) return surface;
  surface.normal = normalize(normal);
  surface.origin = RayOrigin(position, surface.normal, false);
  surface.position = position.xyz;
  surface.depth = position.w;
  surface.texel = texel;
  surface.members = di_members[at >> 3u];
  return surface;
}
RAB_Surface RAB_EmptySurface() { return (RAB_Surface)0; }
bool RAB_IsSurfaceValid(RAB_Surface surface) { return surface.depth > 0.f; }
float3 RAB_GetSurfaceNormal(RAB_Surface surface) { return surface.normal; }
float RAB_GetSurfaceLinearDepth(RAB_Surface surface) { return surface.depth; }

// A DI pass's own pixel: its rays from where rt_lights' start (off a planar surface along the normal from the depth,
// else toward the camera; past a hidden shape with Rays Start Past Hidden Shapes).
RAB_Surface DiCentreSurface(uint2 pixel) {
  RAB_Surface surface = RAB_GetGBufferSurface(int2(pixel), false);
  if (RAB_IsSurfaceValid(surface)) {
    const float4 position = float4(surface.position, surface.depth);
    const float3 normal = SurfaceNormal(int2(pixel), position);
    surface.origin = StartPast(RayOrigin(position, normal, Planar(int2(pixel), position, normal)), position);
  }
  return surface;
}

// Alike for spatial reuse: the same light model, the gloss within 0.1.
struct RAB_Material {
  uint model;
  float gloss;
};
RAB_Material RAB_GetMaterial(RAB_Surface surface) {
  RAB_Material material;
  material.model = surface.texel.w & 0xFFu;
  material.gloss = (float)(surface.texel.x >> 24u) / 255.f;
  return material;
}
bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b) {
  return a.model == b.model && abs(a.gloss - b.gloss) <= 0.1f;
}

int2 RAB_ClampSamplePositionIntoView(int2 pixel, bool previous) {
  return clamp(pixel, 0, int2(width, height) - 1);
}

// This trace's light index as the last trace's (b4's light_previous) or back (DiCurrentLight); -1: none.
int RAB_TranslateLightIndex(uint index, bool current_to_previous) {
  if (index >= 128u) return -1;
  const uint translated = current_to_previous ? LightInLastList(index) : DiCurrentLight(index);
  return (translated < 128u) ? (int)translated : -1;
}

// The temporal pass's bias ray: from this trace's surface toward the sample (this trace's scene: no previous TLAS kept).
bool RAB_GetTemporalConservativeVisibility(RAB_Surface current, RAB_Surface previous, RAB_LightSample light_sample) {
  return DiVisible(current.origin, light_sample.position);
}

#include "Rtxdi/DI/ReservoirStorage.hlsli"

RTXDI_ReservoirBufferParameters DiReservoirParameters() {
  RTXDI_ReservoirBufferParameters parameters = (RTXDI_ReservoirBufferParameters)0;
  parameters.reservoirBlockRowPitch = di_block_row_pitch;
  parameters.reservoirArrayPitch = di_array_pitch;
  return parameters;
}
RTXDI_RuntimeParameters DiRuntimeParameters() {
  RTXDI_RuntimeParameters parameters = (RTXDI_RuntimeParameters)0;
  parameters.neighborOffsetMask = di_offset_mask;
  parameters.frameIndex = frame;
  return parameters;
}
// The bias correction the test switches choose (RTXDI_BIAS_CORRECTION_*; default ray traced; pairwise is the temporal
// pass's basic).
uint DiBiasCorrection() {
  const uint mode = (di_flags >> DI_BIAS_SHIFT) & 3u;
  return (mode == 0u) ? RTXDI_BIAS_CORRECTION_RAY_TRACED
         : (mode == 1u) ? RTXDI_BIAS_CORRECTION_BASIC
         : (mode == 2u) ? RTXDI_BIAS_CORRECTION_PAIRWISE
                        : RTXDI_BIAS_CORRECTION_OFF;
}
#endif

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_RTXDI_BRIDGE_HLSLI_
