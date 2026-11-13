/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_rr_guides (twattila/anti_aliasing.hpp). No water flag: Rome II's water is drawn
// forward, never into the G-buffer, and its third target's alpha is 0 but for the terrain's 1 (terrain_to_gbuffer
// FDDDEDE1) and the skin's own value (the G-buffer PS's o2.w, A2 trace_06): Attila's flag 4 took the ground for water.
#define TW_RR_WATER_FLAG 0u
#include "../twattila/aa_rr_guides.cs_5_0.hlsl"
