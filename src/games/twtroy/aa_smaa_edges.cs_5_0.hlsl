/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila's anti-aliasing shader aa_smaa_edges (twattila/anti_aliasing.hpp). Troy's frame is linear between its ACES tone
// mapper (0C12E608) and hdr_to_screen: its luma edges read it 2.0 encoded (tools/tw/troy/aa_domain).
#define TW_AA_FRAME_LINEAR 1
#include "../twattila/aa_smaa_edges.cs_5_0.hlsl"
