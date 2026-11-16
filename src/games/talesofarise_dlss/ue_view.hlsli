/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_DLSS_UE_VIEW_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_UE_VIEW_HLSLI_

// Unreal's View uniform buffer for the add-ons' own UE passes (ue_sss_setup.hlsli, ue_light_bounce.hlsli), at the
// includer's UE_VIEW_REGISTER. Its registers hold matrix rows (a row vector times the matrix: x row 0 + y row 1 + ...).
// The row numbers are UE 4.21's (Tales of Arise: FViewUniformShaderParameters, the add-on's VIEW_* constants); from
// TranslatedWorldToView (8) on they lie UE_VIEW_SHIFT rows later in the includer's version (4.23, Expeditions: Rome: 4,
// ClipToWorld came before them).
cbuffer UeView : register(UE_VIEW_REGISTER) {
  float4 ue_view[128 + UE_VIEW_SHIFT];
};

#define UE_VIEW(row) ue_view[(row) + (((row) >= 8u) ? UE_VIEW_SHIFT : 0u)]

static const uint UE_TRANSLATED_WORLD_TO_CLIP = 0u;
static const uint UE_TRANSLATED_WORLD_TO_VIEW = 8u;
static const uint UE_VIEW_TO_TRANSLATED_WORLD = 12u;
static const uint UE_VIEW_TO_CLIP = 24u;
static const uint UE_INV_DEVICE_Z_TO_WORLD_Z = 57u;
static const uint UE_SCREEN_POSITION_SCALE_BIAS = 58u;
static const uint UE_PRE_VIEW_TRANSLATION = 62u;
static const uint UE_PREV_TRANSLATED_WORLD_TO_CLIP = 83u;
static const uint UE_PREV_PRE_VIEW_TRANSLATION = 105u;
static const uint UE_VIEW_SIZE_AND_INV_SIZE = 122u;
static const uint UE_BUFFER_SIZE_AND_INV_SIZE = 123u;

#endif  // SRC_GAMES_TALESOFARISE_DLSS_UE_VIEW_HLSLI_
