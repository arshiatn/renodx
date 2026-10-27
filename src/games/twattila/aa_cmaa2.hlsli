/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_AA_CMAA2_HLSLI_
#define SRC_GAMES_TWATTILA_AA_CMAA2_HLSLI_

// CMAA2 (cmaa2/CMAA2.hlsl: Apache 2.0, Copyright (c) 2018 Intel Corporation) on anti_aliasing.hpp's working copy: linear
// BT.2020 (aa_cmaa2_prepare), RGBA16F, written by typed stores; its quality from the settings. Each kernel file names
// its kernel main before including this.
#include "./aa.hlsli"

#define CMAA2_RUNTIME_SETTINGS 1
#define CMAA2_UAV_STORE_TYPED 1
#define CMAA2_UAV_STORE_TYPED_UNORM_FLOAT 0
#define CMAA2_UAV_STORE_CONVERT_TO_SRGB 0
#define CMAA2_SUPPORT_HDR_COLOR_RANGE 1
#define CMAA2_EDGE_DETECTION_LUMA_PATH 1
#include "./cmaa2/CMAA2.hlsl"

#endif  // SRC_GAMES_TWATTILA_AA_CMAA2_HLSLI_
