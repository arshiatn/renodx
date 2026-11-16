/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (talesofarise_dlss/ue_screen_space_shadows.hpp), the dispatch list at Bandai's sun shadow mask
// (cs 0x2084C889): its b0 (ScreenToShadow) and b1 (UE 4.21's View) bound again; the sun from them (sun_direction.hlsli).
#define UE_VIEW_SHIFT 0u
#define UE_VIEW_REGISTER b1
#include "../talesofarise_dlss/ue_view.hlsli"
#include "./sun_direction.hlsli"

cbuffer Shadow : register(b0) {
  float4 shadow[32];
};

// ScreenToWorld: the View's c44-c46.
float4 UeTowardSun() {
  return TowardSun(shadow[28], shadow[29], shadow[30], UE_VIEW(44u).xyz, UE_VIEW(45u).xyz, UE_VIEW(46u).xyz);
}

#include "../talesofarise_dlss/ue_sss_setup.hlsli"
