/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's smoke shadows without rays (twattila/smoke_map.hpp): the particles Smoke Shadows' quad shaders stream out
// (rt_particle_quads: Rome II's three smoke effects, emitter 0 in every record, no weather among them), so no
// behaviour test (its vertex shaders bind no cb3: a stale buffer would be read there).
#define SMOKE_MAP_WITHOUT_BEHAVIOURS
#include "../twattila/smoke_map_list.cs_5_0.hlsl"
