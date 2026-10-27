/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// AO filter (ao_filter.hpp): Temporal Smoothing with the game's MSAA (its G-buffer depth multisampled).
#define AO_FILTER_MS
#include "./ao_filter.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  FilterResolve(int2(id.xy));
}
