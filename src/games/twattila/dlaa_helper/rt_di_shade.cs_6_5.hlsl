/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's shading (PT DI step 4c): each DI pixel's light from its shading reservoir, the game's light
// of the picked light (rt_di_surface.hlsli DiLight) times RTXDI's weight times a final ray to the pick's point of the
// light, into the addon's two DI textures (DiStore); every other render pixel 0 there (an exact pixel's light stays the
// game's, by its bits). NVIDIA's RTXDI-Library 3.2 (NVIDIA RTX SDKs License; rt_rtxdi_bridge.hlsli). This software
// contains source code provided by NVIDIA Corporation.
// Build (build.ps1, RTXDI builds): dxc -T cs_6_5 -D PATH_DI -D PATH_REGIR -I <RTXDI>\Include -Fh rt_di_shade.h
// -Vn RT_DI_SHADE; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_DI_SHADE_OMM.
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"
#include "./rt_light_tiles.hlsli"
#include "./rt_di_surface.hlsli"
#include "./rt_rtxdi_bridge.hlsli"

// The pixel's DI light into the addon's textures, the one place they are written (unified PT's demodulated pair swaps
// this function): the slm light (the composite multiplies the game's SSAO into it) or the skinlm light (no SSAO), the
// other 0; NaN, infinite or negative light 0.
void DiStore(uint2 pixel, uint model, float3 light) {
  const float3 kept = (all(light >= 0.f) && all(light < 65504.f)) ? light : 0.f;
  di_light[0][pixel] = float4((model == TW_POINT_LIGHT_SLM) ? kept : 0.f, 0.f);
  di_light[1][pixel] = float4((model == TW_POINT_LIGHT_SKIN) ? kept : 0.f, 0.f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  const RAB_Surface surface = DiCentreSurface(id.xy);
  float3 light = 0.f;
  if (RAB_IsSurfaceValid(surface)) {
    const RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(DiReservoirParameters(), id.xy, DI_SHADE_ARRAY);
    if (RTXDI_IsValidDIReservoir(reservoir)) {
      const RAB_LightInfo picked = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(reservoir), false);
      const RAB_LightSample light_sample = RAB_SamplePolymorphicLight(picked, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
      const float weight = RTXDI_GetDIReservoirInvPdf(reservoir);
      if (light_sample.light < 128u && weight > 0.f && RAB_GetConservativeVisibility(surface, light_sample)) {
        light = DiLight(surface.texel, surface.position, light_sample.light) * weight;
      }
    }
  }
  DiStore(id.xy, surface.texel.w & 0xFFu, light);
}
