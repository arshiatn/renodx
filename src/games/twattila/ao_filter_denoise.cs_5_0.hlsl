/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// AO filter (ao_filter.hpp): the Denoiser into the sun draws' AO (ao_filter.hlsli).
#include "./ao_filter.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  FilterDenoise(int2(id.xy));
}
