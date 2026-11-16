/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_finish (dlaa.hpp). Divinity 2's DLAA runs on PPApplyLUT's input, the linear
// scene (R11G11B10F): no square root to undo.
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_dlaa_finish.cs_5_0.hlsl"
