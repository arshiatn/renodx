/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// sss01 (screen-space shadows without cascades) with Bend's extended march (FindHit), for SSS
// Softer Away from the Caster (screen_space_shadows.hpp binds it in place of the pass while the row is on; its light
// casters as Light Through Foliage's versions).
#define BEND_SSS_LIGHT_CASTERS
#define BEND_SSS_EXTENDED
#define BEND_SSS_FIND
#include "./shaders/sss.hlsli"
