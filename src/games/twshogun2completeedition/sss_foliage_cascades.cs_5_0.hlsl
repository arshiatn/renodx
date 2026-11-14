/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// sss00 (the sun's shadow mask: screen-space shadows with the cascades) with Bend's light casters, for Light Through
// Foliage (foliage_light.hpp binds it in place of sss00 while the row is on).
#define SSS_SHADOW_CASCADES
#define BEND_SSS_LIGHT_CASTERS
#include "./shaders/sss.hlsli"
