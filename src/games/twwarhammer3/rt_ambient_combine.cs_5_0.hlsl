/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's ray tracing shader rt_ambient_combine (twattila/rt_scene.hpp; Warhammer III's scene too), reading the AO as this
// game's sun draws do.
#define RT_COMBINE_SUN_MAPPING
#include "../twattila/rt_ambient_combine.cs_5_0.hlsl"
