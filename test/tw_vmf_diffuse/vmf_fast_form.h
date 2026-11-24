/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The CPU twin's fast-form table: the numbers of S0's generated vmf_s0/vmf_fast_form.h (fast_final.py, basis r^1..3),
// renamed. R_fast = 1 + sum_k a_k(r) f_k, a_k(r) = r (A_k1 + r (A_k2 + r A_k3)); rows f = 1, t, s, s / t, s^2 / t,
// (1 - u_l^2)(1 - u_v^2) / t, s / (p + 0.3) with s = l.v - u_l u_v, p = u_l u_v, t = u_l + u_v; clamped to [0.05, 4].
// src/games/twattila/vmf_diffuse.hlsli holds the same numbers.
#ifndef TEST_TW_VMF_DIFFUSE_VMF_FAST_FORM_H_
#define TEST_TW_VMF_DIFFUSE_VMF_FAST_FORM_H_

static const float VMF_FAST[7][3] = {
    {0.268330f, -7.200765f, 9.901333f},
    {-0.201940f, 4.948091f, -7.165150f},
    {-2.998354f, 18.247668f, -23.856082f},
    {2.062404f, -8.204499f, 13.571521f},
    {0.021823f, 1.269124f, -1.491314f},
    {-0.008247f, 1.624212f, -0.498470f},
    {2.329960f, -8.867730f, 7.883848f},
};

#endif  // TEST_TW_VMF_DIFFUSE_VMF_FAST_FORM_H_
