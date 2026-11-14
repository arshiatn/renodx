/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_prepare (twattila/anti_aliasing.hpp). Troy's frame is linear between its ACES tone
// mapper (0C12E608, Pharaoh's 976C1C74 twin) and hdr_to_screen: not squared (WARP tools\tw\troy\encoding).
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_dlaa_prepare.cs_5_0.hlsl"
