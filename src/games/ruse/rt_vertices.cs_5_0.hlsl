/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing shader rt_vertices (twattila/rt_core.hpp; R.U.S.E.'s ray_tracing.hpp): the captures' clip
// positions as offsets from the camera (rt_core Host::clip_relative; world coordinates near 2e6).
#define RT_VERTICES_RELATIVE
#include "../twattila/rt_vertices.cs_5_0.hlsl"
