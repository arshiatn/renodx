/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rough diffuse: the vMF diffuse BRDF (Eugene d'Eon and Andrea Weidlich 2024, "VMF Diffuse: A Unified Rough Diffuse
// BRDF", EGSR 2024, Computer Graphics Forum 43(4), DOI 10.1111/cgf.15149; CC BY 4.0,
// https://creativecommons.org/licenses/by/4.0/). Own code from the paper's preprint, changed: its Fourier series with
// the factor 2 on the cosine terms that the preprint omits, and Eq. 9's fm ramped in over r 0.15-0.40. As a ratio to
// Lambert per channel: "base colour x light x N.L" becomes "... x VmfLobe". The microsurface albedo c is chosen per
// channel so that the model's bond albedo is the base colour (the game's brightness kept; only the angular shape and a
// little saturation change). Roughness is the paper's r (its Eq. 2); r 0 returns 1 exactly. Used only where the camera
// sees the surface (receivers): a path's later hits stay Lambert, their caches keep no direction (vmf_diffuse.md B1).
// fxc (SM5) and dxc (SM6.5) alike: fp32 only (no min16float / half), no wave intrinsics, no array indexed by a variable
// (every table loop unrolls to constant indices), no step that needs an infinity (dxc marks float ops fast).
// The constants and tables: vmf_diffuse.h. The CPU twin and the gates: test/tw_vmf_diffuse.
#ifndef SRC_GAMES_TWATTILA_VMF_DIFFUSE_HLSLI_
#define SRC_GAMES_TWATTILA_VMF_DIFFUSE_HLSLI_

#include "vmf_diffuse.h"

// erf by Abramowitz and Stegun 7.1.26 (|error| <= 1.5e-7), odd; HLSL has none. Eq. 4's Beckmann term and Eq. 9's Ca
// take it (HLSL has no local lambdas).
float3 VmfErf(float3 x) {
  const float3 t = 1.f / (1.f + 0.3275911f * abs(x));
  const float3 poly =
      t * (0.254829592f + t * (-0.284496736f + t * (1.421413741f + t * (-1.453152027f + t * 1.061405429f))));
  return (float3)sign(x) * (1.f - poly * exp(-x * x));
}

// The angles both lobes take: the cosines kept off 0, p = u_l u_v, t = u_l + u_v, sines = sin_l sin_v and
// s = sin_l sin_v cos(phi) = l.v - u_l u_v, clamped to the sines (the series' cos(phi) terms carry the sines, so no
// division where a sine is 0; inconsistent inputs stay finite).
struct VmfAngles {
  float u_l;
  float u_v;
  float p;
  float t;
  float sines;
  float s;
};

VmfAngles VmfAnglesOf(float n_dot_l, float n_dot_v, float l_dot_v) {
  VmfAngles angles;
  angles.u_l = clamp(n_dot_l, VMF_MIN_COSINE, 1.f);
  angles.u_v = clamp(n_dot_v, VMF_MIN_COSINE, 1.f);
  angles.p = angles.u_l * angles.u_v;
  angles.t = angles.u_l + angles.u_v;
  angles.sines = sqrt((1.f - angles.u_l * angles.u_l) * (1.f - angles.u_v * angles.u_v));
  angles.s = clamp(l_dot_v - angles.p, -angles.sines, angles.sines);
  return angles;
}

// The paper's r from a guide's linear roughness `rho` (1 - gloss) and the Diffuse Roughness slider (0-1): at slider 1
// the vMF's alpha is the game's specular alpha rho^2 (Eq. 2, alpha = -log(1 - sqrt(r)); design 2.4); r <= 0.3996.
float VmfRoughness(float rho, float slider) {
  const float edge = 1.f - exp(-rho * rho);
  return slider * edge * edge;
}

// pi f(l, v) / base per channel, c matched per channel (brightness kept), clamped to [0, 4]; 1 at r 0, 0 at n.l <= 0,
// faded to 1 below n.v 0.1 (1 at n.v <= 0). R(l, v) = R(v, l) where both cosines are >= 0.1.
float3 VmfLobe(float n_dot_l, float n_dot_v, float l_dot_v, float3 base, float r) {
  if (r <= 0.f) return 1.f;
  if (n_dot_l <= 0.f) return 0.f;
  r = min(r, VMF_MAX_ROUGHNESS);
  const VmfAngles angles = VmfAnglesOf(n_dot_l, n_dot_v, l_dot_v);
  const float p = angles.p;
  const float t = angles.t;
  const float s = angles.s;
  const float squares = angles.u_l * angles.u_l + angles.u_v * angles.u_v;

  // The cross section sigma at u_l, u_v and -u_v by the paper's alpha branches (Eqs. 4-6; Eq. 5 is its own loose fit,
  // 1-6 %, s0_report 1). alpha is floored so Eq. 4's 1 / alpha terms stay finite (r below ~1e-12).
  const float alpha = max(-log(1.f - sqrt(r)), VMF_MIN_ALPHA);
  const float a2 = alpha * alpha;
  const float3 u = float3(angles.u_l, angles.u_v, -angles.u_v);
  float3 sigma;
  if (alpha < 0.25f) {
    // Eq. 4: tanh(1 / alpha^2) from e^(-2 / alpha^2), which underflows to 0 (never an infinity); 1 - u^2 kept off 0 at
    // u 1 (the Beckmann term's e^(u^2 / (alpha^2 (u^2 - 1))) and erf(u / (alpha sqrt(1 - u^2)))).
    const float decay = exp(-2.f / a2);
    const float tanh_term = (1.f - decay) / (1.f + decay);
    const float3 rest = max(1.f - u * u, 1e-12f);
    const float3 width = alpha * sqrt(rest);
    sigma = 0.25f * (tanh_term * (u - a2 * abs(u)) + (-a2 - 2.f) * u + u / tanh_term)
            + 0.5f * (u * (VmfErf(u / width) + 1.f) + width * exp(-u * u / (a2 * rest)) * 0.56418958f);  // 1/sqrt(pi)
  } else if (alpha < 0.9f) {
    // Eq. 5.
    const float3 u2 = u * u;
    const float fall = (1.f - alpha) * (1.f - alpha) * (1.f - alpha) * (1.f - alpha);
    sigma = 0.25f + (0.4975f - 0.273731f * exp2(1.7f * log2(alpha - 0.1f))) * u
            + (0.153918f - 0.139522f * exp2(1.2f * log2(alpha - 0.1f))) * (3.f * u2 - 1.f)
            - (0.013762f + (0.013922f * alpha - 0.027553f) * alpha) * (3.f - 30.f * u2 + 35.f * u2 * u2)
            + fall * (0.048357f * (u2 * u2) * (u2 * u2) - 0.033493f * cos(7.7f * u));
  } else {
    // Eq. 6.
    const float coth_term = 1.f / tanh(2.f / a2);
    sigma = (5.f / 128.f) * (3.f * u * u - 1.f) * (3.f * a2 * a2 - 6.f * a2 * coth_term + 4.f)
            - 0.25f * u * (a2 - 2.f * coth_term) + 0.25f;
  }
  // > 0 in every branch for u > 0 (its power below as exp2 / log2: fxc's X3571 cannot see that).
  const float shadowing = sigma.x * sigma.y;
  // Eq. 8's right-hand side, u_l sigma(-u_v) + u_v sigma(u_l). sigma(-u_v) is not clamped (Eq. 5 dips to -0.006 near
  // u -1): its odd part is linear in every branch, which keeps this symmetric in l and v, and S0's tables were fitted
  // without a clamp. Invariant: denominator >= 0.5 u_l u_v over u in [1e-4, 1]^2, r in (0, 0.40] (the twin's gate;
  // measured minimum 0.62 at r 0.40, u 1).
  const float denominator = angles.u_l * sigma.z + angles.u_v * sigma.x;

  // The r-only coefficients (p. 4, Appendix A pp. 7-9).
  const float r2 = r * r;
  const float r3 = r2 * r;
  const float r4 = r3 * r;
  const float r25 = r2 * sqrt(r);
  const float c100 = 1.f + (0.84f * r4 - 0.1f * r) / (9.f * r3 + 1.f);
  const float c101 = (-9.47f * r3 + 20.4f * r2 + 0.0173f * r) / (7.46f * r + 1.f);
  const float c102 = (2.37f * r2 - 0.927f * r) / (r2 + 1.24f);
  const float c103 = (-1.54f * r2 - 0.11f * r) / (7.1f * r2 - 1.05f * r + 1.f);
  const float c110 = (0.54f * r - 0.182f * r3) / (1.32f * r2 + 1.f);
  const float c111 = (-0.375f * r3 + 0.62f * r2 - 0.097f * r) / (0.4f * r3 + 1.f);
  const float c112 = -0.681f * r2 + 0.862f * r + 0.283f;
  const float c120 = (5.1f * r2 + 2.25f * r) / (32.4f * r2 + 9.8f * r + 1.f);
  const float c121 = (6.f * r3 - 4.32f * r) / (287.f * r3 + 9.7f * r + 1.f);
  const float c200 = (0.226f * r2 + 0.00056f * r) / (7.07f * r2 + 1.f);
  const float c201 = (-12.04f * r3 + 4.57f * r2 - 0.268f * r) / (36.7f * r3 + 1.f);
  const float c202 = (-0.97f * r3 + 2.52f * r2 + 0.418f * r) / (10.f * r2 + 1.f);
  const float c203 = (2.65f * r3 - 2.25f * r2 + 0.068f * r) / (21.4f * r3 + 1.f);
  const float c204 = (0.05f * r - 4.22f * r3) / (43.1f * r3 + 17.6f * r2 + 1.f);
  const float c210 = (-0.027f * r3 - 0.049f * r) / (3.36f * r2 + 1.f);
  const float c211 = (-8.332f * r25 + 6.073f * r3 + 2.77f * r2) / (50.f * r4 + 1.f);
  const float c212 = (-0.295f * r3 - 0.431f * r2) / (23.9f * r3 + 1.f);
  const float c300 = (0.262f * r4 - 0.083f * r3) / (38.6f * r4 - 1.9f * r2 + 1.f);
  const float c301 = (4.95f * r25 - 2.44f * r3 - 0.627f * r2) / (31.5f * r4 + 1.f);
  const float c302 = (0.31f * r25 + 1.4f * r3 + 0.33f * r2) / (20.f * r3 + 1.f);
  const float c303 = (1.77f * r25 - 4.06f * r3 - 0.74f * r2) / (215.f * r4 * r + 1.f);
  const float c304 = -1.026f * r3 / (13.2f * r3 + 5.81f * r2 + 1.f);
  const float c310 = (0.028f * r2 - 0.0132f * r3) / (-3.315f * r4 + 7.46f * r2 + 1.f);
  const float c311 = (0.162f * r25 + 0.302f * r3 - 0.134f * r2) / (57.5f * r4 * sqrt(r) + 1.f);
  const float c312 = (0.5f * r25 - 0.207f * r3 - 0.119f * r2) / (18.7f * r3 + 1.f);

  // pi f_k per unit c^k: f_k(0) + 2 f_k(1) cos(phi) + 2 f_k(2) cos(2 phi) (Section 3.2 with the factor 2: its fitted
  // terms are azimuthal means; design B2, s0_report 1). sin_l sin_v cos(phi) = s, (1 - u_l^2)(1 - u_v^2) cos(2 phi) =
  // 2 s^2 - sines^2.
  const float single =
      shadowing / denominator * (c100 + c101 * p + c102 * p * p + c103 * squares)
      + 2.f * (exp2(c112 * log2(shadowing)) / denominator * s * (c110 + c111 * p)
               + (2.f * s * s - angles.sines * angles.sines) * (c120 + c121 * angles.u_l)
                     * (c120 + c121 * angles.u_v) / t);
  const float second =
      (c200 + c201 * p + c202 * t + c203 * p * p + c204 * squares + 2.f * s * (c210 + c211 * p + c212 * t)) / t;
  const float third =
      (c300 + c301 * p + c302 * t + c303 * p * p + c304 * squares + 2.f * s * (c310 + c311 * p + c312 * t)) / t;

  // beta's row at r, then per channel at the base: piecewise linear as a sum of clamped ramps (no variable index).
  float beta_row[8];
  [unroll] for (uint column = 0; column < 8; ++column) {
    beta_row[column] = VMF_BETA[0][column];
  }
  [unroll] for (uint row = 1; row < 8; ++row) {
    const float ramp = saturate((r - VMF_BETA_R[row - 1]) / (VMF_BETA_R[row] - VMF_BETA_R[row - 1]));
    [unroll] for (uint column = 0; column < 8; ++column) {
      beta_row[column] += ramp * (VMF_BETA[row][column] - VMF_BETA[row - 1][column]);
    }
  }
  const float3 albedo = clamp(base, VMF_MIN_ALBEDO, VMF_MAX_ALBEDO);
  float3 beta = beta_row[0];
  [unroll] for (uint node = 1; node < 8; ++node) {
    const float3 ramp = saturate((albedo - VMF_BETA_BASE[node - 1]) / (VMF_BETA_BASE[node] - VMF_BETA_BASE[node - 1]));
    beta += ramp * (beta_row[node] - beta_row[node - 1]);
  }
  const float3 c = min(albedo / ((1.f - beta) + beta * albedo), VMF_MAX_ALBEDO);

  // Eq. 9, fm (orders 4+): Ck = d'Eon 2021's k_d(c), Ca; ramped in over r VMF_FM_START to + VMF_FM_RANGE.
  const float3 root = sqrt(1.f - c);
  const float3 ck = (-0.453f * (1.f - c) - 0.544f * root + 1.f) / (1.429f * root + 1.f);
  const float3 ca = c * pow(1.169f * root + 1.f, -atan(((r - 0.265f) * r + 0.0225f) * VmfErf(root)));
  const float fm_shape =
      (abs(tanh(r) - 0.09f) - 0.0579f / (p + 0.288f)) * saturate((r - VMF_FM_START) / VMF_FM_RANGE);

  // Eq. 7: f = c f1 + c^2 f2 + c^3 f3 + fm.
  const float3 lobe =
      (c * (single + c * (second + c * third)) + VMF_PI * 0.384f * fm_shape * ck * (ca - 0.342f) * ca) / albedo;
  return lerp(1.f, clamp(lobe, 0.f, VMF_LOBE_MAX), saturate(n_dot_v / VMF_FADE_COSINE));
}

// VmfLobe's scalar shape (c 0.5) for sample loops and ReSTIR targets: its fitted fast form, clamped to [0.05, 4]; 1 at
// r 0, 0 at n.l <= 0, faded to 1 below n.v 0.1. Against VmfLobe at base 0.5: weighted rms <= 1.56 %, max 4.5 %
// (u >= 0.2).
float VmfLobeFast(float n_dot_l, float n_dot_v, float l_dot_v, float r) {
  if (r <= 0.f) return 1.f;
  if (n_dot_l <= 0.f) return 0.f;
  r = min(r, VMF_MAX_ROUGHNESS);
  const VmfAngles angles = VmfAnglesOf(n_dot_l, n_dot_v, l_dot_v);
  const float features[7] = {
      1.f, angles.t, angles.s, angles.s / angles.t, angles.s * angles.s / angles.t,
      angles.sines * angles.sines / angles.t, angles.s / (angles.p + 0.3f)};
  float lobe = 1.f;
  [unroll] for (uint k = 0; k < 7; ++k) {
    lobe += r * (VMF_FAST[k][0] + r * (VMF_FAST[k][1] + r * VMF_FAST[k][2])) * features[k];
  }
  return lerp(1.f, clamp(lobe, VMF_FAST_MIN, VMF_LOBE_MAX), saturate(n_dot_v / VMF_FADE_COSINE));
}

#endif  // SRC_GAMES_TWATTILA_VMF_DIFFUSE_HLSLI_
