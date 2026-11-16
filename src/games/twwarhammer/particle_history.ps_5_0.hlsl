/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smooth Half-Size Particles (Experimental): particle_history.hlsli on Warhammer's camera (Warhammer II's
// light_bounce.hlsli: the game's own camera rows, its viewport origin too), over the game's own half-size particle layer
// (twwarhammer2/particle_smoothing.hpp at E88CBE3C; its alpha the share of the scene it leaves).
#include "../twwarhammer2/light_bounce.hlsli"
#include "../twattila/particle_history.hlsli"
