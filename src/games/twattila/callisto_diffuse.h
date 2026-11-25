/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Callisto diffuse (callisto_diffuse.hlsli): its constants and fixed knob sets, one copy for the shaders and the CPU
// twin (test/tw_callisto_diffuse). The same text is HLSL and C++, so the sets are positional (HLSL has no designated
// initializers). Knobs as on the talk's slide 85: intensity rho, falloff n, tangent falloff m, retroreflection then
// diffuse Fresnel. The exponents are stored as 5 r(x) = 10 (1 - x); an exponent of 0 (falloff 1) folds its factor to 1.
// The knob table these sets come from is gated in test/tw_callisto_diffuse.
#ifndef SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_H_
#define SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_H_

// c1 = (1 + (fresnel - 1) alpha_f) (1 + (retro - 1) alpha_r):
//   alpha_r = (1 - cos_h)^retro_exponent cos_d^retro_tangent_exponent,
//   alpha_f = (1 - cos_d)^fresnel_exponent cos_h^fresnel_tangent_exponent;
// inverse_bond = 1 / the set's bond albedo (cosine-weighted mean of c1 over both hemispheres, unfaded, from the double
// slide model in test/tw_callisto_diffuse), so the lobe keeps the game's brightness: the bond albedo of base x lobe is
// the base.
struct CallistoSet {
  float retro;
  float retro_exponent;
  float retro_tangent_exponent;
  float fresnel;
  float fresnel_exponent;
  float fresnel_tangent_exponent;
  float inverse_bond;
};

// A: the talk's rough fits' retro medians (slides 107-111: rho_r 3, n_r 0.5, m_r 0.75), diffuse Fresnel held at 1
// (its exponents unused: the talk's 0.75 defaults). No fit. Its own PT DI twin (rho_f is already 1).
static const CallistoSet CALLISTO_SET_A = {3.f, 5.f, 2.5f, 1.f, 2.5f, 2.5f, 1.f / 1.0121782f};
// V16: c1 fitted to the merged VmfLobe at r 0.16 (the 40 % default), base 0.5, brightness kept, rho_f >= 0.05 (rho_r
// 1.989, n_r 0.817, m_r 0.665; rho_f 0.050, n_f 0.854, m_f 1.000). Not measured data: vMF's look. rho_f < 1 is vMF's
// microfacet shadowing, allowed at the GI receivers only.
static const CallistoSet CALLISTO_SET_V16 = {1.989f, 1.83f, 3.35f, 0.05f, 1.46f, 0.f, 1.f / 0.9685975f};
// V16's PT DI twin: the same retro, rho_f 1 (Attila's point-light diffuse already carries its own Fresnel-like split).
static const CallistoSet CALLISTO_SET_V16_DI = {1.989f, 1.83f, 3.35f, 1.f, 1.46f, 0.f, 1.f / 1.0521452f};
// V40: c1 fitted to VmfLobe at r 0.40, base 0.5, brightness kept (rho_r 2.480, n_r 0.809, m_r 0.803; rho_f 0.127, n_f
// 0.915, m_f 1.000). Receivers only, as V16.
static const CallistoSet CALLISTO_SET_V40 = {2.48f, 1.91f, 1.97f, 0.127f, 0.85f, 0.f, 1.f / 0.8966906f};
// V40's PT DI twin: rho_f 1.
static const CallistoSet CALLISTO_SET_V40_DI = {2.48f, 1.91f, 1.97f, 1.f, 0.85f, 0.f, 1.f / 1.0833684f};
// Gate controls only, never a player row: a strong retro lobe (rho_r 6.5, n_r 0.9, m_r 0.75), rho_f 1, unkept (inverse
// bond 1; its bond albedo is 1.782, so its mean is +78 %). Bias controls use it in the build and in the reference.
static const CallistoSet CALLISTO_SET_PLANTED = {6.5f, 1.f, 2.5f, 1.f, 2.5f, 2.5f, 1.f};

// The kept lobe's peak up to view 89 is 2.87 (A) / 2.04 (V16) / 2.74 (V40): the cap does not act on the sets that may
// ship; it bounds a bad future set (and a strong diffuse Fresnel's grazing peak).
static const float CALLISTO_LOBE_MAX = 4.f;
static const float CALLISTO_FADE_COSINE = 0.1f;  // the lobe fades to 1 below it (grazing, back-facing normal maps)
static const float CALLISTO_MIN_COSINE = 1e-4f;
// pow's bases are kept >= it: HLSL's pow is exp2(e log2 x), and dxc marks float math fast (no infinities), so
// log2(0) = -inf must never be reached; with it a 0^0 is exp2(0 x -26.6) = 1 and never exp2(0 x -inf) = NaN.
static const float CALLISTO_MIN_BASE = 1e-8f;

#endif  // SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_H_
