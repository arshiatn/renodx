/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// sss_bend below 100% render scale (the hosts pick it by the render share): one sample a screen pixel (O-SS5,
// plan_1117 designs\sss_scale_thickness.md 2.2; bend_sss_gpu.hlsli BEND_SSS_SCREEN_STEPS).
#define BEND_SSS_SCREEN_STEPS
#include "./sss_bend.cs_5_0.hlsl"
