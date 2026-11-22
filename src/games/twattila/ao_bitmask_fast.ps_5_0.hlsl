/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Faster GT-VBAO (Experimental, off): ao_bitmask.ps_5_0.hlsl with sectors equal in angle, cosine-weighted at each
// slice's end (twattila/bitmask.hlsli BITMASK_VARIANT 3, a look change). ao_pass.hpp draws it in ao_bitmask's place
// when the row is on.
#define AO_BITMASK
#define BITMASK_VARIANT 3
#include "./shaders/ssao00_0xBD389EF1.ps_5_0.hlsl"
