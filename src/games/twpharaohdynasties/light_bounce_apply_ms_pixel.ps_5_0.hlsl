/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): the apply for the game's MSAA once per pixel, at sample 0 (Fast MSAA:
// twattila/light_bounce.hpp).
#define LIGHT_BOUNCE_PIXEL
#include "./light_bounce_apply_ms.ps_5_0.hlsl"
