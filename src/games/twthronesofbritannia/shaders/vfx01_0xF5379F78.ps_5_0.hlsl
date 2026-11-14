/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The lit particles' other pixel shader (the same bytes as Attila's): Attila's vfx01 (no shadows, the traced torch lights at
// b9). This game's shared.h first (Attila's include in the file is then skipped: the same guard), its VFX controls in
// vfx_stock.hlsli (VFX Brightness).
#include "../shared.h"
#include "./vfx_stock.hlsli"
#include "../../twattila/shaders/vfx01_0xF5379F78.ps_5_0.hlsl"
