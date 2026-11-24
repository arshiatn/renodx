/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (GT-VBGI, Experimental): the trace pass (twattila/light_bounce_trace.hlsli) on UE 4.23's G-buffer
// (talesofarise_dlss/ue_light_bounce.hlsli).
#define UE_VIEW_SHIFT 4u
#include "../talesofarise_dlss/ue_light_bounce.hlsli"
#include "../twattila/light_bounce_trace.hlsli"
