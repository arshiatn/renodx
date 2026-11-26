/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's fused reuse (PT DI step 4c, ATN's variant; the player keeps rt_di_temporal + rt_di_spatial
// until the numbers say otherwise): one pass in place of the temporal and the spatial one. Each DI pixel's initial
// reservoir merged with the last trace's shading reservoirs: the temporal one where that trace saw the same surface, and
// the spatial neighbours' around it (all from the last trace's reservoirs, surfaces and light places), into this trace's
// shading array. The split passes' settings (DiTemporalParameters, DiSpatialParameters: the same tries, neighbours and M
// cap) and bias correction (ray traced: each merged sample's pdf at its last-trace surface, its ray from there in this
// trace's scene). RTXDI-Library 3.2 has no DI spatiotemporal function: this is its DI temporal and spatial resampling
// (TemporalResampling.hlsli, SpatialResampling.hlsli) in the loop shape of its GI one (GI/SpatioTemporalResampling.hlsli)
// and RTXDI 2's RTXDI_DISpatioTemporalResampling. NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License;
// rt_rtxdi_bridge.hlsli). This software contains source code provided by NVIDIA Corporation.
// Build (only the fused variant, scratch pdi4c_ fused_variant.py; DI_FUSED in every DI pass build of it: the reservoirs'
// two arrays in turns, rt_di_surface.hlsli): dxc -T cs_6_5 -D PATH_DI -D DI_FUSED -D PATH_REGIR -I <RTXDI>\Include
// -Fh rt_di_spatiotemporal.h -Vn RT_DI_SPATIOTEMPORAL; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_DI_SPATIOTEMPORAL_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"
#include "./rt_light_tiles.hlsli"
#include "./rt_di_surface.hlsli"
#include "./rt_rtxdi_bridge.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"
#if !defined(DI_FUSED)
#error "rt_di_spatiotemporal needs -D DI_FUSED (in its rt_di_initial and rt_di_shade builds too): one reservoir array would race"
#endif

// The spatial neighbour `index` of `count` around `centre` in the last trace (RTXDI's stratified offsets).
int2 DiSpatialNeighbour(int2 centre, uint sequence, uint index, uint count, RTXDI_RuntimeParameters runtime, RTXDI_DISpatialResamplingParameters spatial) {
  const uint offset = RTXDI_CalculateSpatialNeighborOffsetIndex(sequence, index, count, runtime, spatial.neighborOffsetSamplingMode);
  return RAB_ClampSamplePositionIntoView(centre + int2(float2(RTXDI_DI_NEIGHBOR_OFFSETS_BUFFER[offset].xy) * spatial.samplingRadius), true);
}

RTXDI_DIReservoir DiSpatioTemporalResampling(uint2 pixel, RAB_Surface surface, RTXDI_DIReservoir current, inout RTXDI_RandomSamplerState rng,
                                             RTXDI_RuntimeParameters runtime, RTXDI_ReservoirBufferParameters reservoirs, float3 motion,
                                             RTXDI_DITemporalResamplingParameters temporal, RTXDI_DISpatialResamplingParameters spatial) {
  const uint history_limit = min(RTXDI_PackedDIReservoir_MaxM, uint(temporal.maxHistoryLength * current.M));
  // The selected light by the last trace's list (the bias correction's pdfs at the last trace's surfaces).
  int selected_previous = RTXDI_IsValidDIReservoir(current) ? RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(current), true) : -1;
  RTXDI_DIReservoir state = RTXDI_EmptyDIReservoir();
  RTXDI_CombineDIReservoirs(state, current, 0.5f, current.targetPdf);
  int2 centre = int2(round(float2(pixel) + motion.xy));
  const float expected_depth = RAB_GetSurfaceLinearDepth(surface) + motion.z;

  // The temporal sample (RTXDI_DITemporalResampling's search): nine tries around the reprojected pixel, then the fallback
  // at the pixel itself; the first similar surface is the one merged, and the spatial neighbours are around it.
  const int tries = 9;
  bool fallback = false;
  bool found = false;
  RAB_Surface temporal_surface = RAB_EmptySurface();
  [loop]
  for (int i = 0; i < tries + (temporal.enableFallbackSampling ? 1 : 0); ++i) {
    fallback = i == tries;
    int2 at = fallback ? int2(pixel) : centre;
    if (i != 0 && !fallback) at += int2((float2(RTXDI_GetNextRandom(rng), RTXDI_GetNextRandom(rng)) - 0.5f) * 4.f);
    if ((temporal.enablePermutationSampling && i == 0) || fallback) RTXDI_ApplyPermutationSampling(at, temporal.uniformRandomNumber);
    temporal_surface = RAB_GetGBufferSurface(at, true);
    if (!RAB_IsSurfaceValid(temporal_surface)) continue;
    if (!fallback && !RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporal_surface), expected_depth,
                                            RAB_GetSurfaceLinearDepth(temporal_surface), temporal.normalThreshold, temporal.depthThreshold)) {
      continue;
    }
    centre = at;
    found = true;
    break;
  }
  fallback = fallback && found;

  // The samples merged: 0 the temporal one (bit 0 of `merged`), 1.. the spatial neighbours; each one's M (capped) and
  // whether it was picked.
  uint merged = 0u;
  int picked = -1;
  float temporal_m = 0.f;
  // The neighbours' count by the history after the temporal sample (the split spatial pass's by its input's M).
  const uint history_m = current.M + (found ? min(RTXDI_LoadDIReservoir(reservoirs, RTXDI_PixelPosToReservoirPos(centre, runtime.activeCheckerboardField),
                                                                        DI_HISTORY_ARRAY).M, history_limit) : 0u);
  const uint count = min(((history_m < spatial.targetHistoryLength) ? max(spatial.numDisocclusionBoostSamples, spatial.numSamples) : spatial.numSamples), 31u);
  const uint sequence = RTXDI_InitializeSpatialNeighborSequence(RTXDI_GetNextRandom(rng), runtime, spatial.neighborOffsetSamplingMode);
  [loop]
  for (uint sample = 0u; sample <= count; ++sample) {
    if (sample == 0u && !found) continue;
    const int2 at = (sample == 0u) ? centre : DiSpatialNeighbour(centre, sequence, sample - 1u, count, runtime, spatial);
    RAB_Surface neighbour = temporal_surface;
    if (sample != 0u) {
      neighbour = RAB_GetGBufferSurface(at, true);
      if (!RAB_IsSurfaceValid(neighbour)) continue;
      if (!RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(neighbour), expected_depth, RAB_GetSurfaceLinearDepth(neighbour),
                                 spatial.normalThreshold, spatial.depthThreshold)) {
        continue;
      }
      if (spatial.enableMaterialSimilarityTest && !RAB_AreMaterialsSimilar(RAB_GetMaterial(surface), RAB_GetMaterial(neighbour))) continue;
    }
    RTXDI_DIReservoir previous = RTXDI_LoadDIReservoir(reservoirs, RTXDI_PixelPosToReservoirPos(at, runtime.activeCheckerboardField), DI_HISTORY_ARRAY);
    previous.M = min(previous.M, history_limit);
    previous.spatialDistance = (fallback && sample == 0u) ? int2(0, 0) : previous.spatialDistance + (at - centre);
    previous.age = (fallback && sample == 0u) ? 0u : previous.age + 1u;
    const uint original_light = RTXDI_GetDIReservoirLightIndex(previous);
    if (RTXDI_IsValidDIReservoir(previous)) {
      const int mapped = RAB_TranslateLightIndex(original_light, false);
      previous.weightSum = (mapped < 0) ? 0.f : previous.weightSum;
      previous.lightData = (mapped < 0) ? 0u : (uint(mapped) | RTXDI_DIReservoir_LightValidBit);
    }
    merged |= 1u << sample;
    temporal_m = (sample == 0u) ? previous.M : temporal_m;
    float weight = 0.f;
    if (RTXDI_IsValidDIReservoir(previous)) {
      const RAB_LightSample candidate = RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(previous), false), surface,
                                                                  RTXDI_GetDIReservoirSampleUV(previous));
      weight = RAB_GetLightSampleTargetPdfForSurface(candidate, surface);
    }
    if (RTXDI_CombineDIReservoirs(state, previous, RTXDI_GetNextRandom(rng), weight)) {
      picked = int(sample);
      selected_previous = int(original_light);
    }
  }

#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
  if (temporal.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC && RTXDI_IsValidDIReservoir(state)) {
    // The selected sample's pdf at each merged sample's last-trace surface (RTXDI's MIS-like normalization, not 1/M).
    float pi = state.targetPdf;
    float pi_sum = state.targetPdf * current.M;
    if (selected_previous >= 0) {
      const RAB_LightInfo selected_light = RAB_LoadLightInfo(selected_previous, true);
      [loop]
      for (uint sample = 0u; sample <= count; ++sample) {
        if ((merged & (1u << sample)) == 0u) continue;
        const int2 at = (sample == 0u) ? centre : DiSpatialNeighbour(centre, sequence, sample - 1u, count, runtime, spatial);
        RAB_Surface neighbour = temporal_surface;  // no struct conditional in HLSL
        if (sample != 0u) neighbour = RAB_GetGBufferSurface(at, true);
        const RAB_LightSample at_neighbour = RAB_SamplePolymorphicLight(selected_light, neighbour, RTXDI_GetDIReservoirSampleUV(state));
        float ps = RAB_GetLightSampleTargetPdfForSurface(at_neighbour, neighbour);
#if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
        // Each merged sample's ray from its own last-trace surface (the temporal sample's too, as the split temporal pass).
        if (temporal.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0.f
            && !RAB_GetTemporalConservativeVisibility(surface, neighbour, at_neighbour)) {
          ps = 0.f;
        }
#endif
        const float m = (sample == 0u) ? temporal_m
                                       : (float)min(RTXDI_LoadDIReservoir(reservoirs, RTXDI_PixelPosToReservoirPos(at, runtime.activeCheckerboardField),
                                                                          DI_HISTORY_ARRAY).M, history_limit);
        pi = (picked == int(sample)) ? ps : pi;
        pi_sum += ps * m;
      }
    }
    RTXDI_FinalizeResampling(state, pi, pi_sum);
  } else
#endif
  {
    RTXDI_FinalizeResampling(state, 1.f, state.M);
  }
  return state;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex) {
  DiCurrentClear(index);
  GroupMemoryBarrierWithGroupSync();
  DiCurrentBuild(index);
  GroupMemoryBarrierWithGroupSync();
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  const RAB_Surface surface = RAB_GetGBufferSurface(int2(id.xy), false);
  if (!RAB_IsSurfaceValid(surface)) return;
  const RTXDI_ReservoirBufferParameters reservoirs = DiReservoirParameters();
  RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(reservoirs, id.xy, DI_INITIAL_ARRAY);
  if ((di_flags & (DI_HISTORY | DI_NO_TEMPORAL)) == DI_HISTORY) {
    RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_DI_SPATIOTEMPORAL));
    reservoir = DiSpatioTemporalResampling(id.xy, surface, reservoir, rng, DiRuntimeParameters(), reservoirs, DiMotion(id.xy, surface),
                                           DiTemporalParameters(), DiSpatialParameters());
  }
  RTXDI_StoreDIReservoir(reservoir, reservoirs, id.xy, DI_SHADE_ARRAY);
}
