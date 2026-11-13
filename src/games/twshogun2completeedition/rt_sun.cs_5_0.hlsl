/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing shader rt_sun (twattila/rt_core.hpp; Shogun 2's ray_tracing.hpp).
// Shogun 2's lighting_VS_PS has no ambient scale switch (c12 is g_cloud_shadow_lerp): its sun light shaders light the
// diffuse colour by sun_colour (c1) x N.L x the shadow unscaled. The ambient cube as uploaded (the shaders' x
// g_ambient_fudge_factor c14.y not in it: no Path-Traced Ambient row here).
#define TW_RT_SUN_SCALE_SWITCH 0
#include "../twattila/rt_sun.cs_5_0.hlsl"
