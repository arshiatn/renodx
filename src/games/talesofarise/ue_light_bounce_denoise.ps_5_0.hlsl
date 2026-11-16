/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (GT-VBGI, Experimental): the denoise pass (twattila/light_bounce_denoise.hlsli) on UE 4.21's G-buffer
// (talesofarise_dlss/ue_light_bounce.hlsli).
#define UE_VIEW_SHIFT 0u
#include "../talesofarise_dlss/ue_light_bounce.hlsli"
#include "../twattila/light_bounce_denoise.hlsli"
