/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GBUFFER_HLSLI_
#define SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GBUFFER_HLSLI_

// Light Bounce (Experimental): the sun draw's G-buffer channels as each game's light_bounce.hlsli declares and reads
// them: single-sampled, or with LIGHT_BOUNCE_MSAA (the game's MSAA: light_bounce.hpp draws the *_ms builds of the
// prepare and the apply) multisampled, read at bounce_sample (the prepare: sample 0; the apply: each sample's own, or
// sample 0 with LIGHT_BOUNCE_PIXEL).
#ifdef LIGHT_BOUNCE_MSAA
#define BounceGBuffer Texture2DMS<float4>
static uint bounce_sample = 0u;

float4 BounceLoad(Texture2DMS<float4> channel, int2 texel) {
  return channel.Load(texel, bounce_sample);
}
#else
#define BounceGBuffer Texture2D<float4>

float4 BounceLoad(Texture2D<float4> channel, int2 texel) {
  return channel.Load(int3(texel, 0));
}
#endif

#endif  // SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GBUFFER_HLSLI_
