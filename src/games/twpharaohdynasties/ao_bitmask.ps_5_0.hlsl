/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ambient Occlusion Visibility Bitmask: the addon's own shader in place of the game's ssao01 (twattila/ao_pass.hpp),
// compiled from that pass's file.
#define AO_BITMASK
#include "./shaders/ssao01_0xD42C3523.ps_5_0.hlsl"
