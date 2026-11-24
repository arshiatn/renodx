/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The CPU twin's beta(r, base) table: the numbers of S0's generated vmf_s0/vmf_beta_table.h (vmf_s0.cpp `beta`, its
// nodes kBetaR / kBetaBase), renamed. c = base / ((1 - beta) + beta base) makes the corrected vMF model's bond albedo the
// base; bilinear, clamped to the end nodes. src/games/twattila/vmf_diffuse.hlsli holds the same numbers; the WARP gates
// compare its lookups with the twin's at every node.
#ifndef TEST_TW_VMF_DIFFUSE_VMF_BETA_TABLE_H_
#define TEST_TW_VMF_DIFFUSE_VMF_BETA_TABLE_H_

static const float VMF_BETA_R[8] = {0.f, 0.02f, 0.05f, 0.10f, 0.16f, 0.24f, 0.32f, 0.40f};
static const float VMF_BETA_BASE[8] = {0.02f, 0.15f, 0.30f, 0.45f, 0.60f, 0.75f, 0.85f, 0.95f};
static const float VMF_BETA[8][8] = {
    {0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f},
    {0.016858f, 0.018028f, 0.019916f, 0.022830f, 0.027926f, 0.039131f, 0.059045f, 0.158602f},
    {0.033822f, 0.034732f, 0.036180f, 0.038387f, 0.042211f, 0.050569f, 0.065378f, 0.139313f},
    {0.068462f, 0.068659f, 0.068869f, 0.069070f, 0.069285f, 0.069583f, 0.069986f, 0.071758f},
    {0.115125f, 0.114767f, 0.113867f, 0.112174f, 0.108967f, 0.101762f, 0.089027f, 0.026334f},
    {0.186922f, 0.188848f, 0.190257f, 0.190719f, 0.189701f, 0.184666f, 0.172102f, 0.089476f},
    {0.269814f, 0.277933f, 0.285696f, 0.292419f, 0.298468f, 0.302328f, 0.296300f, 0.199706f},
    {0.370430f, 0.387799f, 0.404265f, 0.419443f, 0.435674f, 0.454284f, 0.463913f, 0.400478f},
};

#endif  // TEST_TW_VMF_DIFFUSE_VMF_BETA_TABLE_H_
