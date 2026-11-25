/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's temporal pass (PT DI step 4c): each DI pixel's initial reservoir merged with the last trace's
// shading reservoir where that trace saw the same surface (RTXDI's DITemporalResampling: the pixel reprojected by the
// last trace's camera, nine tries around it and a fallback, the light indices by b4's light_previous; ray-traced bias
// correction from this trace's surface), into DI_TEMPORAL_ARRAY; without history (DI_HISTORY) the initial one as it is.
// NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License; rt_rtxdi_bridge.hlsli). This software contains source code
// provided by NVIDIA Corporation.
// Build (build.ps1, RTXDI builds): dxc -T cs_6_5 -D PATH_DI -D PATH_REGIR -I <RTXDI>\Include -Fh rt_di_temporal.h
// -Vn RT_DI_TEMPORAL; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_DI_TEMPORAL_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"
#include "./rt_light_tiles.hlsli"
#include "./rt_di_surface.hlsli"
#include "./rt_rtxdi_bridge.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"

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
    RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_DI_TEMPORAL));
    int2 temporal_pixel;
    RAB_LightSample picked = RAB_EmptyLightSample();
    reservoir = RTXDI_DITemporalResampling(id.xy, surface, reservoir, rng, DiRuntimeParameters(), reservoirs, DiMotion(id.xy, surface),
                                           DI_HISTORY_ARRAY, DiTemporalParameters(), temporal_pixel, picked);
  }
  RTXDI_StoreDIReservoir(reservoir, reservoirs, id.xy, DI_TEMPORAL_ARRAY);
}
