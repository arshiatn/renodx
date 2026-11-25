/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Callisto diffuse: the diffuse Fresnel and retroreflection lobe c1 of the Callisto BRDF (Jorge Jimenez and Miguel
// Petersen, "The Rendering of The Callisto Protocol", SIGGRAPH 2023 Advances in Real-Time Rendering in Games, slide 85;
// the BRDF developed with Jose Naranjo and Miguel Rodriguez). Own code from the slides' formulas, diffuse only,
// brightness kept: no smooth terminator (c2), no Proxima, no specular term; the game's specular stays its own. As a
// ratio to Lambert: "base colour x light x N.L" becomes "... x CallistoLobe". Each knob set's provenance is in
// callisto_diffuse.h. Used only where the camera sees the surface (receivers).
// fxc (SM5) and dxc (SM6.5) alike: fp32 only, no wave intrinsics, no array indexed by a variable, no step that needs
// an infinity (dxc marks float ops fast). The CPU twin and the gates: test/tw_callisto_diffuse.
#ifndef SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_HLSLI_
#define SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_HLSLI_

#include "callisto_diffuse.h"

// base^exponent as HLSL's pow does it, exp2(exponent log2(base)), for the lobe's four factors (HLSL has no local
// lambdas). Exponent 0 (a falloff of 1) is 1: the compare is on a set's constant, so both compilers fold it. The base
// is clamped too, so dxc's fast math never meets log2(0) = -inf (0 x -inf = NaN in IEEE, undefined under fast math).
float CallistoPower(float base, float exponent) {
  return (exponent == 0.f) ? 1.f : exp2(exponent * log2(max(base, CALLISTO_MIN_BASE)));
}

// c1 / bond for the set, capped at CALLISTO_LOBE_MAX, blended in by `amount` (0 = Lambert) and faded to 1 below n.v
// 0.1. 1 at amount <= 0, n.l <= 0 (the caller's cosine is 0 there: wrap and transmission terms stay unchanged) and
// n.v <= 0. Exactly reciprocal in l and v where n.l and n.v >= 0.1 (c1 depends on theta_d and theta_h only).
float CallistoLobe(CallistoSet set, float n_dot_l, float n_dot_v, float l_dot_v, float amount) {
  if (amount <= 0.f) return 1.f;
  if (n_dot_l <= 0.f) return 1.f;
  if (n_dot_v <= 0.f) return 1.f;
  const float u_l = clamp(n_dot_l, CALLISTO_MIN_COSINE, 1.f);
  const float u_v = clamp(n_dot_v, CALLISTO_MIN_COSINE, 1.f);
  // cos theta_d = l.h = sqrt((1 + l.v) / 2), cos theta_h = n.h = (n.l + n.v) / sqrt(2 + 2 l.v); l = -v keeps the
  // rsqrt finite.
  const float cos_d = sqrt(saturate(0.5f + 0.5f * l_dot_v));
  const float cos_h = saturate((u_l + u_v) * rsqrt(max(2.f + 2.f * l_dot_v, CALLISTO_MIN_BASE)));
  const float retro_mask =
      CallistoPower(1.f - cos_h, set.retro_exponent) * CallistoPower(cos_d, set.retro_tangent_exponent);
  const float fresnel_mask =
      CallistoPower(1.f - cos_d, set.fresnel_exponent) * CallistoPower(cos_h, set.fresnel_tangent_exponent);
  const float c1 = (1.f + (set.fresnel - 1.f) * fresnel_mask) * (1.f + (set.retro - 1.f) * retro_mask);
  return lerp(1.f, min(c1 * set.inverse_bond, CALLISTO_LOBE_MAX), amount * saturate(n_dot_v / CALLISTO_FADE_COSINE));
}

#endif  // SRC_GAMES_TWATTILA_CALLISTO_DIFFUSE_HLSLI_
