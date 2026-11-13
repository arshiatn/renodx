/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_dlaa_prepare (twattila/anti_aliasing.hpp). Rome II's frame is linear between its tone
// mapper (EE1DA45C) and hdr_to_screen, which gamma-encodes it as it is: not squared.
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_dlaa_prepare.cs_5_0.hlsl"
