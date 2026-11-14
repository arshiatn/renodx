/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's ray tracing shader rt_sun (twattila/rt_scene.hpp; the same game shader here). WH1's lighting_VS_PS float 44
// (Attila's ambient scale switch) is g_hdr_on: its sun light shaders take the ambient cube x 1 always, and light the
// G-buffer's diffuse colour by sun_colour (c1) x N.L x the shadow x 0.318310 x (g_hdr_on > 0 ? 0.01 : 1) (all 31 the
// same; test/tw_warhammer_shaders runs 230F2241's bytes on WARP): the paths' sun with g_hdr_on 1.
#define TW_RT_SUN_SCALE_SWITCH 0
#define TW_RT_SUN_LIGHT_SCALE  0.0031831f
#include "../twattila/rt_sun.cs_5_0.hlsl"
