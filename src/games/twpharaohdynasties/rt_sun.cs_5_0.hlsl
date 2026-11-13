/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's ray tracing shader rt_sun (twattila/rt_scene.hpp; the same game shader here). Pharaoh's sun light shaders take
// their ambient cube x 1 (no scale switch: lighting_VS_PS float 60 is g_cloud_shadow_position.x here), and light the
// G-buffer's diffuse colour (albedo x (1 - the specular colour), the guide's) by sun_colour (c3) x N.L x the shadow x
// 0x389FB405 (7.615242e-05) x g_debug_light_diffuse_coef (1 in every trace): F883F5B2's bytes, the literal in all 25.
#define TW_RT_SUN_SCALE_SWITCH 0
#define TW_RT_SUN_LIGHT_SCALE asfloat(0x389FB405u)
#include "../twattila/rt_sun.cs_5_0.hlsl"
