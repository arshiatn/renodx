/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_BLOOM_HIGH_PASS_HLSLI_
#define SRC_GAMES_TWATTILA_BLOOM_HIGH_PASS_HLSLI_

// Troy's, Pharaoh's, Warhammer's and Warhammer II's hdr_tone_mapper high pass of one source pixel (bloom_filter.hlsli's
// BloomHighPassTap): its luminance (BLOOM_LUMINANCE_FROM_COLOR: from its colour, the constant tone map's; else its alpha)
// above `cutoff`, the colour x (luminance - cutoff - 1), capped (the game's bloom.hlsli CapBloomSource), x the shader's
// g_bloom_strength (alpha too); below the cutoff (0, 0, 0, 1). Include after the shader's declarations and bloom.hlsli.
float4 BloomHighPassTap(float4 source, float cutoff) {
#ifdef BLOOM_LUMINANCE_FROM_COLOR
  const float luminance = dot(source.rgb, float3(0.212599993f, 0.715200007f, 0.0722000003f));
#else
  const float luminance = source.a;
#endif
  const float3 bloom = max(0.f, CapBloomSource(source.rgb * (luminance - cutoff) - source.rgb));
  return (luminance >= cutoff) ? g_bloom_strength * float4(bloom, 1.f) : float4(0.f, 0.f, 0.f, 1.f);
}

#include "./bloom_filter.hlsli"

#endif  // SRC_GAMES_TWATTILA_BLOOM_HIGH_PASS_HLSLI_
