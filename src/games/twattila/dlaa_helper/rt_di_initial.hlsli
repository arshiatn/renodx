/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's initial sampling (PT DI step 4c; rt_di_initial.hlsl runs it a pixel a thread, unified
// PT's primary pass can call the same): RTXDI's streamed RIS over the pixel's tile's listed lights, uniform candidates
// (min(listed, DI_CANDIDATES) of them, source pdf 1 / listed), the target the game's light (rt_di_surface.hlsli DiTarget),
// then the pick's visibility ray (an invisible pick discarded, its M kept: RTXDI's enableInitialVisibility). After
// rt_rtxdi_bridge.hlsli (PATH_DI).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_INITIAL_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_INITIAL_HLSLI_

static const uint DI_CANDIDATES = 8u;

// The tile's list's light at `slot` (rt_lights' TileList: light_mask's tile words; without tiles the lights' order).
uint DiListLight(uint2 tile, uint slot) {
  if (light_tile_row == 0u) return slot;
  return (light_mask[TileWord(tile, slot >> 2u)] >> ((slot & 3u) * 8u)) & 0xFFu;
}

// The reservoir of a DI pixel's surface (its rays from its stored origin: rt_di_initial) at `pixel`, and its pick's sample.
RTXDI_DIReservoir DiInitialSample(uint2 pixel, RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out RAB_LightSample picked) {
  picked = RAB_EmptyLightSample();
  RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
  const uint listed = countbits(surface.members.x) + countbits(surface.members.y) + countbits(surface.members.z) + countbits(surface.members.w);
  if (listed == 0u) return reservoir;
  const uint candidates = min(listed, DI_CANDIDATES);
  [loop]
  for (uint candidate = 0u; candidate < candidates; ++candidate) {
    const uint slot = min((uint)(RTXDI_GetNextRandom(rng) * (float)listed), listed - 1u);
    const RAB_LightInfo light = RAB_LoadLightInfo(DiListLight(pixel >> 3u, slot), false);
    const float2 uv = float2(RTXDI_GetNextRandom(rng), RTXDI_GetNextRandom(rng));
    const RAB_LightSample light_sample = RAB_SamplePolymorphicLight(light, surface, uv);
    if (RTXDI_StreamSample(reservoir, light.index, uv, RTXDI_GetNextRandom(rng), RAB_GetLightSampleTargetPdfForSurface(light_sample, surface),
                           (float)listed)) {
      picked = light_sample;
    }
  }
  RTXDI_FinalizeResampling(reservoir, 1.f, reservoir.M);
  reservoir.M = 1.f;
  if (RTXDI_IsValidDIReservoir(reservoir) && !RAB_GetConservativeVisibility(surface, picked)) {
    RTXDI_StoreVisibilityInDIReservoir(reservoir, 0.f, true);
  }
  return reservoir;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_INITIAL_HLSLI_
