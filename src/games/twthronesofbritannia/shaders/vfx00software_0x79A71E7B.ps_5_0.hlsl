/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The lit particles' main pixel shader (vfx00), another build (the same bytes as Attila's):
// twattila/shaders/vfx00software_0x79A71E7B.ps_5_0.hlsl.
// This game's shared.h first (Attila's include in the file is then skipped: the same guard), its VFX controls in
// vfx_stock.hlsli (VFX Brightness).
#include "../shared.h"
#include "./vfx_stock.hlsli"
#include "../../twattila/shaders/vfx00software_0x79A71E7B.ps_5_0.hlsl"
