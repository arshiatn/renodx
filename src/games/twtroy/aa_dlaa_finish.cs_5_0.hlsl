/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_finish (twattila/anti_aliasing.hpp). Troy's frame is linear between its ACES tone
// mapper (0C12E608) and hdr_to_screen: given back linear (WARP tools\tw\troy\encoding).
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_dlaa_finish.cs_5_0.hlsl"
