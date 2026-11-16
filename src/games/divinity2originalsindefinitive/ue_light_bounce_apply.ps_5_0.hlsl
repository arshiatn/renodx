/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// GT-VBGI (screen_space.hpp): the apply pass (twattila/light_bounce_apply.hlsli) on Divinity 2's G-buffer
// (bounce_gbuffer.hlsli), named as talesofarise_dlss/ue_light_bounce.hpp embeds it: added to the scene after
// ClusteredLightingWithSSAO, x the diffuse colour.
#include "./bounce_gbuffer.hlsli"
#include "../twattila/light_bounce_apply.hlsli"
