/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smooth Half-Size Particles (Experimental): particle_history.hlsli on Warhammer II's camera (light_bounce.hlsli: the game's
// own camera rows, its viewport origin too), over the game's own half-size particle layer (particle_smoothing.hpp; its alpha
// the share of the scene it leaves).
#include "./light_bounce.hlsli"
#include "../twattila/particle_history.hlsli"
