/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (GT-VBGI, Experimental): the apply pass (twattila/light_bounce_apply.hlsli) on UE 4.23's G-buffer
// (talesofarise_dlss/ue_light_bounce.hlsli): added to the scene colour after the sun's light, x the diffuse colour.
#define UE_VIEW_SHIFT 4u
#include "../talesofarise_dlss/ue_light_bounce.hlsli"
#include "../twattila/light_bounce_apply.hlsli"
