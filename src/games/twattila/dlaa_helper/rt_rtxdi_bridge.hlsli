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
  // start) world position, its distance from the camera (positions' w; 0: no DI surface), its view Z (RTXDI's linear
  // depth), light surface texel and its tile's listed lights.
  float3 position;
  float depth;
  float view_depth;
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
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:117 RAB_LoadLightInfo (prevFrame: the last frame's light).
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
// A point of the light's disk (light_size, under Ray Reconstruction; else its centre) seen from the surface's ray
// origin, `uv` in 0-1 each.
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:160 RAB_SamplePolymorphicLight.
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo light, RAB_Surface surface, float2 uv) {
  RAB_LightSample light_sample = RAB_EmptyLightSample();
  // guard inert: a light_count past b4's 128 lights; proof pending S5G; trace HARNESS_LIGHT_STRESS=125 (128 lights).
  if (light.index < min(light_count, 128u)) {
    light_sample.position = lights[light.index].xyz;
    if (!(light_size <= 0.f) && (flags & RECONSTRUCTED) != 0u) {
      light_sample.position += DiskPoint(normalize(light_sample.position - surface.origin), light_size, uv);
    }
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
// RTXDI's target: the light's game light's luminance where the surface's tile lists it (`members`), else 0 (an unlisted
// light keeps the game's unshadowed light); NaN and negative 0.
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:148 RAB_GetLightSampleTargetPdfForSurface.
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample light_sample, RAB_Surface surface) {
  // dev: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:148: target 0 for a light the tile does not list; such a
  // light keeps the game's unshadowed light.
  if (light_sample.light >= 128u || SetBit(surface.members, light_sample.light) == 0u) return 0.f;
  const float target = Luminance(DiLight(surface.texel, surface.position, light_sample.light));
  return (target > 0.f) ? target : 0.f;  // guard output: a light at the surface's point (d = 0) gives NaN, read 0; box S10
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
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:166 RAB_GetConservativeVisibility.
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
// reservoirs (two arrays, rt_di_surface.hlsli DI_*_ARRAY), its spatial neighbours' offsets (RTXDI's
// FillNeighborOffsetBuffer, stratified, as floats) and the two lights the shading writes (rt_di_shade).
RWStructuredBuffer<RTXDI_PackedDIReservoir> di_reservoirs : register(u80);
#define RTXDI_LIGHT_RESERVOIR_BUFFER di_reservoirs
RWBuffer<float2> di_offsets : register(u81);
#define RTXDI_DI_NEIGHBOR_OFFSETS_BUFFER di_offsets
RWTexture2D<float4> di_light[2] : register(u82);  // the light SSAO dims (slm), the light it does not (skinlm)
// Each DI pixel's ray origin from its position (rt_di_initial), this trace's at DiParity(), the last's at the other.
RWTexture2D<float4> di_origin[2] : register(u84);

// The DI pixel's surface at `pixel` (this trace's, or the last's: its position and light surface; this trace's camera
// and tile members, a stated bias), else an empty one: off the screen, sky, no point-light model, or not a DI pixel in
// that trace (its light history's word not DI_PIXEL_WORD: its reservoirs were not written). Its rays start where that
// pixel's own rays started (di_origin, written by that trace's rt_di_initial): RTXDI's "visibility computed in the same
// way" for the spatial and fused neighbours' bias rays as for the pixel's own (RtxdiApplicationBridge.md,
// RAB_GetConservativeVisibility; fa1_'s B1: they started toward the camera, never past a hidden shape).
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:73 RAB_GetGBufferSurface.
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
  // ours: a zero normal (no light surface written there) is no surface, as a model the light shaders do not light.
  if ((model != TW_POINT_LIGHT_SLM && model != TW_POINT_LIGHT_SKIN) || !(dot(normal, normal) > 1e-6f)) return surface;
  surface.normal = normalize(normal);
  surface.origin = position.xyz + di_origin[previous ? 1u - DiParity() : DiParity()][at].xyz;
  surface.position = position.xyz;
  surface.depth = position.w;
  // src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:103 RAB_GetSurfaceLinearDepth (motion .z must be this depth's change): the view Z of the
  // camera that saw the surface, as DiMotion's .z (review pdi4cr_ S2: the camera distance against a view Z change).
  surface.view_depth = ClipOf(position.xyz, previous ? previous_view_projection : view_projection).w;
  surface.texel = texel;
  surface.members = di_members[at >> 3u];
  return surface;
}

// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:85 RAB_IsSurfaceValid, spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:103 RAB_GetSurfaceLinearDepth.
RAB_Surface RAB_EmptySurface() { return (RAB_Surface)0; }
bool RAB_IsSurfaceValid(RAB_Surface surface) { return surface.depth > 0.f; }
float3 RAB_GetSurfaceNormal(RAB_Surface surface) { return surface.normal; }
float RAB_GetSurfaceLinearDepth(RAB_Surface surface) { return surface.view_depth; }

// Alike for spatial reuse: the same light model, the gloss within 0.1.
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:257 RAB_AreMaterialsSimilar.
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
  // ours: a gloss step of 0.1 (26 of 255) or less is alike; picked, not measured.
  return a.model == b.model && abs(a.gloss - b.gloss) <= 0.1f;
}

// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:109 RAB_ClampSamplePositionIntoView.
int2 RAB_ClampSamplePositionIntoView(int2 pixel, bool previous) {
  // src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:111 the pixel clamped into the render size, the function's contract.
  return clamp(pixel, 0, int2(width, height) - 1);
}

// This trace's light index as the last trace's (b4's light_previous) or back (DiCurrentLight); -1: none.
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:139 RAB_TranslateLightIndex.
int RAB_TranslateLightIndex(uint index, bool current_to_previous) {
  // src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:141 -1 for a light with no index in the other list.
  if (index >= 128u) return -1;
  const uint translated = current_to_previous ? LightInLastList(index) : DiCurrentLight(index);
  return (translated < 128u) ? (int)translated : -1;  // src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:141 (-1: none)
}

// The temporal pass's bias ray: from the history's surface, its own stored ray origin (di_origin of the last trace),
// toward the sample.
// src: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:174 RAB_GetTemporalConservativeVisibility (the previous surface against the previous scene).
// dev: spec:rtxdi@fdbd9b3:Doc/RtxdiApplicationBridge.md:174: this trace's TLAS, not the last's (none is kept): exact for still shapes; a shape that moved since is tested where
// it is now, RTXDI's "current scene" fallback for that part (review pdi4cr_ S1: from the current surface instead, a
// permuted history pixel shadowed for the canonical light darkened W by up to 1 / (1 + M) that frame).
bool RAB_GetTemporalConservativeVisibility(RAB_Surface current, RAB_Surface previous, RAB_LightSample light_sample) {
  return DiVisible(previous.origin, light_sample.position);
}

#include "Rtxdi/DI/ReservoirStorage.hlsli"

// src: spec:rtxdi-lib@b67f61c:Include/Rtxdi/RtxdiUtils.h:34 the host's reservoir layout (CalculateReservoirBufferParameters) and
// spec:rtxdi-lib@b67f61c:Source/ReSTIRDI.cpp:133 the neighbour offsets' mask, from b2.
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
// ours: a test switch (RENODX_TW_DI_TEST) over RTXDI's four bias correction modes.
uint DiBiasCorrection() {
  const uint mode = (di_flags >> DI_BIAS_SHIFT) & 3u;
  return (mode == 0u) ? RTXDI_BIAS_CORRECTION_RAY_TRACED
         : (mode == 1u) ? RTXDI_BIAS_CORRECTION_BASIC
         : (mode == 2u) ? RTXDI_BIAS_CORRECTION_PAIRWISE
                        : RTXDI_BIAS_CORRECTION_OFF;
}

// The temporal reuse's settings (the split temporal pass's and the fused pass's): 20 frames of history, nine tries
// around the reprojected pixel and a fallback, the permutation.
// src: spec:rtxdi-lib@b67f61c:Source/ReSTIRDI.cpp:53 GetDefaultReSTIRDITemporalResamplingParams (20, 0.1, 0.5, permutation on), the bias
// correction ray traced (DiBiasCorrection).
RTXDI_DITemporalResamplingParameters DiTemporalParameters() {
  RTXDI_DITemporalResamplingParameters parameters = (RTXDI_DITemporalResamplingParameters)0;
  parameters.maxHistoryLength = 20u;
  parameters.biasCorrectionMode = DiBiasCorrection();
  parameters.depthThreshold = 0.1f;
  parameters.normalThreshold = 0.5f;
  parameters.enablePermutationSampling = 1u;
  parameters.uniformRandomNumber = di_uniform_random;
  parameters.enableFallbackSampling = 1u;
  return parameters;
}
// The spatial reuse's (the split spatial pass's and the fused pass's): two neighbours within 32 pixels, eight on a
// history under four, the same light model and gloss, stratified offsets; none with DI_NO_SPATIAL.
// src: spec:rtxdi-lib@b67f61c:Source/ReSTIRDI.cpp:75 GetDefaultReSTIRDISpatialResamplingParams (2, boost 8, 32 px, 0.1, 0.5, material test,
// stratified), the bias correction ray traced (DiBiasCorrection).
RTXDI_DISpatialResamplingParameters DiSpatialParameters() {
  RTXDI_DISpatialResamplingParameters parameters = (RTXDI_DISpatialResamplingParameters)0;
  const bool spatial = (di_flags & DI_NO_SPATIAL) == 0u;
  parameters.numSamples = spatial ? 2u : 0u;
  parameters.numDisocclusionBoostSamples = spatial ? 8u : 0u;
  parameters.samplingRadius = 32.f;
  parameters.biasCorrectionMode = DiBiasCorrection();
  parameters.depthThreshold = 0.1f;
  parameters.normalThreshold = 0.5f;
  // ours: 4, not RTXDI's 0 (ReSTIRDI.cpp:86: no boost), so a pixel with under four frames of history takes the eight
  // boost neighbours; no change at a still camera (the 640 harness A/B, lab G-1126-69 / -70), the moving-camera A/B is box S4 (pdi4cr_).
  parameters.targetHistoryLength = 4u;
  parameters.enableMaterialSimilarityTest = 1u;
  parameters.neighborOffsetSamplingMode = RTXDI_DI_NEIGHBOR_OFFSET_SAMPLING_MODE_STRATIFIED_LOW_DISCREPANCY;
  return parameters;
}
// The pixel's motion to where the last trace's camera saw its surface (pixel centres), and its depth's change (0: behind
// that camera). Ours: the camera's motion only (RTXDI takes the application's full motion vectors), as every helper
// history reprojects (rt_camera.hlsli SeenByLastTrace, the Light Bounce's reuse): no helper ray-tracing shader reads the
// game's object motion. A moved unit's surface fails the temporal pass's depth / normal tests (DiTemporalParameters)
// and takes fresh samples: less reuse there, no stale light (fa1_ U1 measures it).
// dev: spec:rtxdi-lib@b67f61c:Include/Rtxdi/DI/TemporalResampling.hlsli:29: the camera's motion only, not the game's object motion; no
// helper ray-tracing shader reads the game's motion vectors.
float3 DiMotion(uint2 pixel, RAB_Surface surface) {
  const float4 clip = ClipOf(surface.position, previous_view_projection);
  if (!(clip.w > 0.f)) return 0.f;  // dev: spec:rtxdi-lib@b67f61c:Include/Rtxdi/DI/TemporalResampling.hlsli:29: behind the last camera no motion; the fallback finds it
  const float2 seen = (clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f) * float2(width, height) - 0.5f;
  return float3(seen - float2(pixel), clip.w - ClipOf(surface.position, view_projection).w);
}
#endif

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_RTXDI_BRIDGE_HLSLI_
