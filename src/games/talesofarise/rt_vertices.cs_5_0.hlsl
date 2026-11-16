/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's (the helper's scene: ray_tracing.hpp), with the masks read at a scaled UV (rt_core Host::mask_uv_scales: the
// masked prepass's UV times a material register).
#define RT_VERTICES_UV_SCALES
#include "../twattila/rt_vertices.cs_5_0.hlsl"
