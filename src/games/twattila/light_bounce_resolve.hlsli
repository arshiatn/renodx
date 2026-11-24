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
// apply reads). Directional Light Bounce (LB.directional, bitmask_sh.hlsli): the trace's (g, w) and zonal amplitude
// (t56-t57) and the history's (t58-t59) blended with the bounce's weights into SV_Target2-3, the history's scaled as its
// light was clipped and its g held within the 3 x 3's own (rvs_ S5), then w pinned at the texel's normal last, after
// every blend, clip and scale, so the texel stays exact at its normal whatever the history brought (shg_ S0 12.2).
// Game-neutral: the includer's light_bounce.hlsli first.
#include "./bitmask_sh.hlsli"
#include "./light_bounce.h"
#include "./light_bounce_geometry.hlsli"
#include "./light_bounce_history.hlsli"

Texture2D<float4> t_traced : register(t40);  // the trace's output (alpha: its distance; negative: its history already)
Texture2D<float> t_history_frames : register(t48);  // the last history's frame count per texel
Texture2D<float4> t_traced_sh : register(t56);      // the trace's (g, w)
Texture2D<float> t_traced_zonal : register(t57);    // the trace's zonal amplitude

void main(float4 v0 : SV_Position0, out float4 o0 : SV_Target0, out float o1 : SV_Target1
#if defined(LIGHT_BOUNCE_DIRECTIONAL)
          , out float4 o2 : SV_Target2, out float o3 : SV_Target3) {
#else
          ) {
  float4 o2;
  float o3;
#endif
  const int2 texel = int2(v0.xy);
  const float4 traced = t_traced.Load(int3(texel, 0));
  o0 = traced;
  o1 = 1.f;
  o2 = 0.f;
  o3 = 0.f;
  const bool directional = BOUNCE_DIRECTIONAL;
  [branch]
  if (directional) {
    o2 = t_traced_sh.Load(int3(texel, 0));
    o3 = t_traced_zonal.Load(int3(texel, 0));
  }
  if (!(abs(traced.a) > 0.f) || !(LB.history > 0.f)) return;
  // Where this texel was: the count there (the nearest texel's).
  const BounceGeometry geometry = LoadGeometry(texel, 0u, 0u);
  const float3 position = geometry.position;
  const float3 previous = BouncePrevious(position);
  const float frames = t_history_frames.Load(int3(int2(floor(previous.xy / LB.scale)), 0));
  // Checkerboard: the trace kept the history (no new frame).
  if (traced.a < 0.f) {
    o0 = float4(traced.rgb, -traced.a);
    o1 = frames;
    [branch]
    if (directional) {
      const BitmaskShLight pinned = BitmaskShPinned(o2.xyz, o3, BitmaskShFacing(geometry.normal, -position / length(position)));
      o2 = float4(pinned.g, pinned.w);
      o3 = pinned.zonal;
    }
    return;
  }
  float3 sum = 0.f;
  float3 squares = 0.f;
  float count = 0.f;
  float3 g_sum = 0.f;
  float3 g_squares = 0.f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const float4 neighbour = t_traced.Load(int3(texel + int2(x, y), 0));
      if (abs(abs(neighbour.a) - traced.a) < 0.049988326f * traced.a) {
        sum += neighbour.rgb;
        squares += neighbour.rgb * neighbour.rgb;
        count += 1.f;
        [branch]
        if (directional) {
          const float3 g = t_traced_sh.Load(int3(texel + int2(x, y), 0)).xyz;
          g_sum += g;
          g_squares += g * g;
        }
      }
    }
  }
  const float3 mean = sum / count;
  const float3 spread = sqrt(max(squares / count - mean * mean, 0.f));
  float4 history_sh;
  float history_zonal;
  const float4 history = BounceHistory(position, directional, history_sh, history_zonal);
  [flatten]
  if (history.a > 0.009999861f) {
    // Fewer than 5 of the same surface (thin things, edges): their spread says nothing (one alone has none, which would
    // hold the history to this frame's noise).
    const float3 width = (count >= 5.f) ? max(1.5f * spread, 0.1f * mean) : 1e30f;
    const float3 held = clamp(history.rgb, mean - width, mean + width);
    // Partly the same surface: fewer frames trusted.
    const float trusted = frames * saturate(2.f * history.a);
    const float kept = min(trusted / (trusted + 1.f), LB.history);
    o0.rgb = lerp(traced.rgb, held, kept);
    o1 = min(trusted + 1.f, 32.f);
    // Directional: the history's direction and zonal amplitude as its light was clipped (at most 4 x), its g then held
    // within the 3 x 3's: per component mean +- 1.5 x spread, at least 10% of the mean light's luminance.
    [branch]
    if (directional) {
      const float history_y = dot(history.rgb, BITMASK_SH_LUMINANCE);
      const float clip_scale = (history_y > 0.f) ? min(dot(held, BITMASK_SH_LUMINANCE) / history_y, 4.f) : 1.f;
      const float3 g_mean = g_sum / count;
      const float3 g_spread = sqrt(max(g_squares / count - g_mean * g_mean, 0.f));
      const float3 g_width = (count >= 5.f) ? max(1.5f * g_spread, 0.1f * dot(mean, BITMASK_SH_LUMINANCE)) : 1e30f;
      const float3 g_held = clamp(history_sh.xyz * clip_scale, g_mean - g_width, g_mean + g_width);
      o2.xyz = lerp(o2.xyz, g_held, kept);
      o3 = lerp(o3, history_zonal * clip_scale, kept);
    }
  }
  [branch]
  if (directional) {
    const BitmaskShLight pinned = BitmaskShPinned(o2.xyz, o3, BitmaskShFacing(geometry.normal, -position / length(position)));
    o2 = float4(pinned.g, pinned.w);
    o3 = pinned.zonal;
  }
}
