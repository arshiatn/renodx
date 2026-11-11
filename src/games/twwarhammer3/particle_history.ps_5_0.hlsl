/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smooth Half-Size Particles (Experimental): twattila/particle_history.hlsli on Warhammer III's camera, over the game's
// half-size particle layer (its colour sums, and beside them its zeroth moment: the optical depth).
#define PARTICLE_OPTICAL_DEPTH
#include "./light_bounce.hlsli"
#include "../twattila/particle_history.hlsli"
