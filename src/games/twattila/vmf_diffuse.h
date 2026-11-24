/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// vMF diffuse (vmf_diffuse.hlsli): its constants and S0's fitted tables, one copy for the shaders and the CPU twin
// (test/tw_vmf_diffuse). The same text is HLSL and C++.
#ifndef SRC_GAMES_TWATTILA_VMF_DIFFUSE_H_
#define SRC_GAMES_TWATTILA_VMF_DIFFUSE_H_

static const float VMF_PI = 3.14159265f;
static const float VMF_MAX_ROUGHNESS = 0.40f;  // the paper's first porous r (its Fig. 6); VmfRoughness stays under it
// R = pi f / base with c matched (c > base): it reaches the clamp at u 0.1 (4.5-5.3 at r 0.40, base <= 0.5,
// backscatter) and pi f / c reaches 6.6 at u 0.05 (s0_report 3; vms1_review N7).
static const float VMF_LOBE_MAX = 4.f;
static const float VMF_FAST_MIN = 0.05f;     // the fast form's floor for weights and targets (s0_report 5)
static const float VMF_MIN_ALBEDO = 1e-4f;   // R divides by the clamped base: HDR base > 1 never gives R < 1
static const float VMF_MAX_ALBEDO = 0.999f;  // the paper's Eq. 9 takes sqrt(1 - c)
static const float VMF_MIN_COSINE = 1e-4f;   // the model's 1 / (u_l + u_v) terms stay finite
static const float VMF_MIN_ALPHA = 1e-6f;    // Eq. 4's 1 / alpha terms stay finite (r below ~1e-12 is Lambert in fp32)
static const float VMF_FADE_COSINE = 0.1f;   // R fades to 1 below it (grazing and back-facing normal-mapped pixels)
// Eq. 9's fm is wrong below r ~0.2 (negative where the walk's orders 4+ are < 0.4 %): ramped in over r 0.15-0.40
// (s0_report 1-2).
static const float VMF_FM_START = 0.15f;
static const float VMF_FM_RANGE = 0.25f;

// beta(r, base) for c = base / ((1 - beta) + beta base): the corrected model's bond albedo (cosine-weighted mean of its
// directional albedo) equals base, rms 0.08 %, max 0.20 % (s0_report 4; the numbers of vmf_s0/vmf_beta_table.h, nodes
// vmf_s0.cpp kBetaR / kBetaBase). Bilinear between the nodes, clamped to the end nodes.
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

// The scalar fast form R = 1 + sum_k a_k(r) f_k, a_k(r) = r (A_k1 + r (A_k2 + r A_k3)), fitted to the corrected model
// at base 0.5 (brightness kept) jointly over r (s0_report 5; the numbers of vmf_s0/vmf_fast_form.h, fast_final.py
// basis r^1..3): weighted rms <= 1.56 %, max 4.5 % for u >= 0.2. Rows: f = 1, t, s, s / t, s^2 / t,
// (1 - u_l^2)(1 - u_v^2) / t, s / (p + 0.3) with p = u_l u_v, t = u_l + u_v, s = l.v - p.
static const float VMF_FAST[7][3] = {
    {0.268330f, -7.200765f, 9.901333f},
    {-0.201940f, 4.948091f, -7.165150f},
    {-2.998354f, 18.247668f, -23.856082f},
    {2.062404f, -8.204499f, 13.571521f},
    {0.021823f, 1.269124f, -1.491314f},
    {-0.008247f, 1.624212f, -0.498470f},
    {2.329960f, -8.867730f, 7.883848f},
};

#endif  // SRC_GAMES_TWATTILA_VMF_DIFFUSE_H_
