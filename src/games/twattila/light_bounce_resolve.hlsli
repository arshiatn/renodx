/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental), the resolve (light_bounce.hpp): this frame's trace (t40) and the last frames' bounce at
// this point (light_bounce_history.hlsli, t41: moved with the camera, kept where it was the same surface), the history
// held within this frame's neighbourhood: the 3 x 3 traced texels of the same surface give a mean and a spread per
// channel, and the history is clamped to mean +- 1.5 x spread, at least 10% of the mean (variance clipping, Salvi 2016;
// with fewer than 5 of the same surface only the distance check; as SVGF's temporal step,
// Schied et al. 2017), so a soldier walking by leaves no trail although only the camera's motion is known. The history's
// frames counted (t48 last frame's, SV_Target1 this one's; as SVGF's history length): a newly seen spot averages its
// first frames evenly, then at most LB.history of the history. Into the history (and, without the denoiser, what the
// apply reads). Game-neutral: the includer's light_bounce.hlsli first.
#include "./light_bounce.h"
#include "./light_bounce_geometry.hlsli"
#include "./light_bounce_history.hlsli"

Texture2D<float4> t_traced : register(t40);  // the trace's output (alpha: its distance; negative: its history already)
Texture2D<float> t_history_frames : register(t48);  // the last history's frame count per texel

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out float o1 : SV_Target1) {
  const int2 texel = int2(v0.xy);
  const float4 traced = t_traced.Load(int3(texel, 0));
  o0 = traced;
  o1 = 1.f;
  if (!(abs(traced.a) > 0.f) || !(LB.history > 0.f)) return;
  // Where this texel was: the count there (the nearest texel's).
  const float3 position = LoadGeometry(texel, 0u, 0u).position;
  const float3 previous = BouncePrevious(position);
  const float frames = t_history_frames.Load(int3(int2(floor(previous.xy / LB.scale)), 0));
  // Checkerboard: the trace kept the history (no new frame).
  if (traced.a < 0.f) {
    o0 = float4(traced.rgb, -traced.a);
    o1 = frames;
    return;
  }
  float3 sum = 0.f;
  float3 squares = 0.f;
  float count = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const float4 neighbour = t_traced.Load(int3(texel + int2(x, y), 0));
      if (abs(abs(neighbour.a) - traced.a) < 0.05f * traced.a) {
        sum += neighbour.rgb;
        squares += neighbour.rgb * neighbour.rgb;
        count += 1.f;
      }
    }
  }
  const float3 mean = sum / count;
  const float3 spread = sqrt(max(squares / count - mean * mean, 0.f));
  const float4 history = BounceHistory(position);
  [flatten]
  if (history.a > 0.01f) {
    // Fewer than 5 of the same surface (thin things, edges): their spread says nothing (one alone has none, which would
    // hold the history to this frame's noise).
    const float3 width = (count >= 5.f) ? max(1.5f * spread, 0.1f * mean) : 1e30f;
    const float3 held = clamp(history.rgb, mean - width, mean + width);
    // Partly the same surface: fewer frames trusted.
    const float trusted = frames * saturate(2.f * history.a);
    o0.rgb = lerp(traced.rgb, held, min(trusted / (trusted + 1.f), LB.history));
    o1 = min(trusted + 1.f, 32.f);
  }
}
