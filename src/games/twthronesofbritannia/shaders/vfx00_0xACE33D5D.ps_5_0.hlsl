/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The lit particles' main pixel shader (the same bytes as Attila's): Attila's vfx00, which reads the rays' shadow (t49) at
// its render pixel inside Particle Resolution's layer and the traced torch lights (b9). This game's shared.h first (Attila's
// include in the file is then skipped: the same guard), its VFX controls in vfx_stock.hlsli (VFX Brightness).
#include "../shared.h"
#include "./vfx_stock.hlsli"
#include "../../twattila/shaders/vfx00_0xACE33D5D.ps_5_0.hlsl"
