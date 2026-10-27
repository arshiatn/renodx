/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ambient Occlusion Visibility Bitmask: the addon's own shader in place of the game's ssao00 (twattila/ao_pass.hpp),
// compiled from that pass's file.
#define AO_BITMASK
#include "./shaders/ssao00_0xBD389EF1.ps_5_0.hlsl"
