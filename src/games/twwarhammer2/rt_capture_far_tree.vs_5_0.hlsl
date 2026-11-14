/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer II's far trees (rigidtreebillboard vs40_main, 0xC1A857A3): Pharaoh's card capture (the same vertex shader code:
// the card turned to the camera at v0.x across, v0.y up, x v3.w, at v2.xyz; the face from v3.xz's angle to the view), turned
// to the sun. Its vertex shader binds no tree constants: every card the game draws (its pixel shader 72E40C15 discards those
// nearer than the cull distance, b1 of the pixel shader only).
#define RT_FAR_TREE_NO_CONSTANTS
#include "../twpharaohdynasties/rt_capture_far_tree.vs_5_0.hlsl"
