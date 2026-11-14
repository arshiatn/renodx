/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing shader rt_vertices (twattila/rt_core.hpp; Rome Remastered's ray_tracing.hpp): the captures'
// clip positions as offsets from the camera (rt_core Host::clip_relative; near 1 m, far 25 km).
#define RT_VERTICES_RELATIVE
#include "../twattila/rt_vertices.cs_5_0.hlsl"
