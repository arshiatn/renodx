/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// sss01 (screen-space shadows without the cascades) with Bend's light casters, for Light Through Foliage
// (foliage_light.hpp binds it in place of sss01 while the row is on).
#define BEND_SSS_LIGHT_CASTERS
#include "./shaders/sss.hlsli"
