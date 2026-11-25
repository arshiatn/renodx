/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Torch Light in Path-Traced GI's ReGIR cells (PT DI step 3, T2; a paths request's, before its radiance cache and paths,
// rt_d3d12.hpp): a thread a cell slot, NVIDIA's RTXDI-Library 3.2 ReGIR build (RTXDI_PresampleLocalLightsForReGIR; NVIDIA
// RTX SDKs License, compiled from the unmodified copy build.ps1 finds: not part of this file) over b4's lights through
// rt_rtxdi_bridge.hlsli. This software contains source code provided by NVIDIA Corporation.
// Build (build.ps1, with its RTXDI copy): dxc -T cs_6_5 -D PATH_REGIR -I <RTXDI>\Include -Fh rt_regir_presample.h
// -Vn RT_REGIR_PRESAMPLE.
#include "./rt_scene.hlsli"
#include "./rt_rtxdi_bridge.hlsli"
#include "Rtxdi/LightSampling/PresamplingFunctions.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  RTXDI_RandomSamplerState rng = RabSampler(uint4(id.xy, frame, DIM_REGIR_BUILD));
  // The power-RIS candidates' tile pick only (unused: uniform candidates).
  RTXDI_RandomSamplerState coherent = rng;
  RTXDI_PresampleLocalLightsForReGIR(rng, coherent, id.x, RegirLights(), (RTXDI_RISBufferSegmentParameters)0, regir);
}
