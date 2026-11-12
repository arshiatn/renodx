/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's ray tracing shader rt_sun (twattila/rt_scene.hpp; the same game shader here). Pharaoh's sun light shaders take
// their ambient cube x 1 (no scale switch: lighting_VS_PS float 60 is g_cloud_shadow_position.x here).
#define TW_RT_SUN_SCALE_SWITCH 0
#include "../twattila/rt_sun.cs_5_0.hlsl"
