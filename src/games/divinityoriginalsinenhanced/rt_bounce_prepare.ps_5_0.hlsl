/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Global Illumination through the rays (ray_tracing.hpp): the light leaving what the camera sees (the scene before
// DeferredFog, every light in), one texel per scale x scale render pixels.
#include "../twattila/rt_bounce_prepare.hlsli"
