/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ambient Occlusion Visibility Bitmask at Half resolution: Warhammer's AO blur output from the half-size results
// (twwarhammer2/line_sweep_ao.hpp, twwarhammer2/ao_bitmask_blur.hlsli).
#define AO_UPSAMPLE
#include "./shared.h"
#include "../twwarhammer2/ao_bitmask_blur.hlsli"
