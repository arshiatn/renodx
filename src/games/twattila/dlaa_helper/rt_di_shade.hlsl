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
// other 0.
// ours: the one writer of the addon's two DI textures, the pixel's light by its model.
void DiStore(uint2 pixel, uint model, float3 light) {
  // guard inert: NaN or infinite light to 0, light past the half floats' largest clamped there (none in a normal frame:
  // DiLight's colours at 0 and up, B2); proof pending S6; trace S6's one-frame count.
  const float3 kept = (all(isfinite(light)) ? min(light, 65504.f) : 0.f);
  // src: spec:dxspecs@ba0b51c:d3d/archive/D3D11_3_FunctionalSpec.htm:617 a store into a smaller float format rounds toward zero.
  // ours: half the texture format's ULP at the value's binade added first, so the store rounds to nearest (the binade at
  // least 2^-14, both formats' smallest normal: 0 stays 0): round-to-zero into R11G11B10's 6 / 6 / 5 mantissa bits lost
  // ~0.58 % of DI's luminance (DI-interior E against the bits 0.9933 -> 0.9986, review pdi4cr_ M2 C5), into
  // R16G16B16A16's 10 ~0.04 %.
  const float3 binade = exp2(floor(log2(max(kept, exp2(-14.f)))));
  // src: spec:dxspecs@ba0b51c:d3d/archive/D3D11_3_FunctionalSpec.htm:553 each texture's mantissa bits: R11G11B10's 6 / 6 / 5, else R16G16B16A16's 10 (IEEE half).
  const float3 bits[2] = {(((di_flags & DI_LIGHT_R11G11B10) != 0u) ? float3(6.f, 6.f, 5.f) : 10.f),
                          (((di_flags & (DI_LIGHT_R11G11B10 << 1)) != 0u) ? float3(6.f, 6.f, 5.f) : 10.f)};
  di_light[0][pixel] = float4((model == TW_POINT_LIGHT_SLM) ? kept + 0.5f * binade * exp2(-bits[0]) : 0.f, 0.f);
  di_light[1][pixel] = float4((model == TW_POINT_LIGHT_SKIN) ? kept + 0.5f * binade * exp2(-bits[1]) : 0.f, 0.f);
}

// src: spec:rtxdi@fdbd9b3:Samples/FullSample/Shaders/LightingPasses/DI/ShadeSamples.hlsl:33 the shading pass: the reservoir's light times its weight, behind a final visibility ray.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  SceneRayFlags();
  if (id.x >= width || id.y >= height) return;
  const RAB_Surface surface = RAB_GetGBufferSurface(int2(id.xy), false);
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
