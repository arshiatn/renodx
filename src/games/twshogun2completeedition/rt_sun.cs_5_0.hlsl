/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing shader rt_sun (twattila/rt_core.hpp; Shogun 2's ray_tracing.hpp).
// Shogun 2's lighting_VS_PS has no ambient scale switch: nine registers past its cube (c3) the shared one would read
// c12.x, g_cloud_shadow_lerp (0.5 in the data run's battle): the unscaled cube always. Its sun light shaders
// (sunlight00-03) light the diffuse colour by sun_colour (c1) x N.L x the shadow x 1.90384671e-005 (three copies in
// each): the paths' sun in the game's units. The ambient cube as uploaded (the shaders' x g_ambient_fudge_factor c14.y
// not in it: no Path-Traced Ambient row here). test/tw_shogun2_shaders checks both on WARP.
#define TW_RT_SUN_SCALE_SWITCH 0
#define TW_RT_SUN_LIGHT_SCALE  1.90384671e-005f
#include "../twattila/rt_sun.cs_5_0.hlsl"
