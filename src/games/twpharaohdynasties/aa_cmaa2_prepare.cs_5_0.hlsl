/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_cmaa2_prepare (twattila/anti_aliasing.hpp). Pharaoh's frame is linear between its ACES
// tone mapper (976C1C74) and hdr_to_screen: no 2.0 decode into the working copy (tools/tw/troy/aa_domain).
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_cmaa2_prepare.cs_5_0.hlsl"
