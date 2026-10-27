/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_SSR_MARCH_HLSLI_
#define SRC_GAMES_TWATTILA_SSR_MARCH_HLSLI_

// The screen-space reflections' hierarchical march: FidelityFX SSSR's traversal (ffx_sssr/ffx_sssr.h, MIT, Copyright
// (c) 2021 Advanced Micro Devices) over the depth's min pyramid (t10, built before the draw by ssr_hiz.hpp), its hit
// refined and checked. The includer defines FFX_SSSR_LoadWorldSpaceNormal and FFX_SSSR_ScreenSpaceToViewSpace (in its
// own space: Attila's view space, Rome II's water world space) and SI (ssr_hierarchical) first, and SSR_VIEW_SIZE (the
// view's size in pixels: DLSS below 100% draws it into the top-left of the screen-size textures) where it knows it;
// else the pyramid's size is the view's.
Texture2D<float> t_ssr_hiz : register(t10);

// A level's texel; one past its last row or column (FidelityFX halves the size without rounding down) is the last.
float FFX_SSSR_LoadDepth(int2 pixel, int mip)
{
  uint width, height, levels;
  t_ssr_hiz.GetDimensions(mip, width, height, levels);
  const int2 size = int2(width, height);
  if (all(pixel <= size)) pixel = min(pixel, size - 1);
  return t_ssr_hiz.Load(int3(pixel, mip));
}

#define FFX_SSSR_NO_WAVE_OPS
#include "./ffx_sssr/ffx_sssr.h"

// The pyramid is there (built this frame) and the setting is on.
bool SsrHierarchicalReady()
{
  uint width, height, levels;
  t_ssr_hiz.GetDimensions(0, width, height, levels);
  return SI.ssr_hierarchical != 0.f && width != 0;
}

// A ray from `origin` (screen: uv, depth 0 near) along `direction` (its end minus `origin`, in screen space;
// `direction_world` the same for FidelityFX's back-face check), at most `max_steps`: its hit (refined: where, between a
// texel before it and it, the ray first goes behind the full-resolution depth, 4 halvings: contact edges in place to a
// fraction of a texel) and the confidence (FidelityFX's checks: on screen, not the sky, not a back face, within a
// thickness of the surface of a quarter metre plus 2% of its distance from `camera` (the includer's space), faded at an
// 8% screen border: a softer switch to what the miss shows); 0: none.
float SsrTrace(float3 origin, float3 direction, float3 direction_world, uint max_steps, float3 camera, out float3 hit)
{
#ifdef SSR_VIEW_SIZE
  const float2 screen_size = SSR_VIEW_SIZE;
#else
  uint width, height, levels;
  t_ssr_hiz.GetDimensions(0, width, height, levels);
  const float2 screen_size = float2(width, height);
#endif
  bool valid_hit;
  hit = FFX_SSSR_HierarchicalRaymarch(origin, direction, true, screen_size, 0, 0, max_steps, valid_hit);
  [branch]
  if (!valid_hit) return 0.f;
  const float along = dot(hit.xy - origin.xy, direction.xy) / max(dot(direction.xy, direction.xy), 1e-12f);
  const float texel = length(1.f / screen_size) / max(length(direction.xy), 1e-6f);
  float before = max(along - texel, 0.f);
  float after = along;
  [unroll]
  for (int halving = 0; halving < 4; ++halving)
  {
    const float middle = 0.5f * (before + after);
    const float3 sample_position = origin + direction * middle;
    const bool behind = sample_position.z >= FFX_SSSR_LoadDepth(int2(sample_position.xy * screen_size), 0);
    after = behind ? middle : after;
    before = behind ? before : middle;
  }
  hit = origin + direction * after;
  const float thickness = 0.25f + 0.02f * length(FFX_SSSR_ScreenSpaceToViewSpace(hit) - camera);
  const float2 border = smoothstep(0.f, 0.08f, hit.xy) * (1.f - smoothstep(0.92f, 1.f, hit.xy));
  return FFX_SSSR_ValidateHit(hit, origin.xy, direction_world, screen_size, thickness) * border.x * border.y;
}

#endif  // SRC_GAMES_TWATTILA_SSR_MARCH_HLSLI_
