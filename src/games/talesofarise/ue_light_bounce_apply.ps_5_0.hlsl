/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (GT-VBGI, Experimental): the apply pass (twattila/light_bounce_apply.hlsli) on UE 4.21's G-buffer
// (talesofarise_dlss/ue_light_bounce.hlsli), as the light arriving (x Strength): lighting00-05 multiply their own base
// colour into it (rt_ambient.hlsli's t42, as the ray-traced bounce).
#define UE_VIEW_SHIFT 0u
#define UE_BOUNCE_IRRADIANCE
#include "../talesofarise_dlss/ue_light_bounce.hlsli"
#include "../twattila/light_bounce_apply.hlsli"
