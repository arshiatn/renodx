/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_TWWARHAMMER2_LINE_SWEEP_BLUR_HLSLI_
#define SRC_TWWARHAMMER2_LINE_SWEEP_BLUR_HLSLI_

// ssao.hlsl cs50_blur_x, written from the disassembly (Warhammer II / Troy / Pharaoh: identical code; Warhammer: its AO
// is half size, LINE_SWEEP_HALF_SIZE_AO): the AO's horizontal depth-aware blur, 11 taps 5 pixels apart. The game's
// code only: with the Visibility Bitmask the addon dispatches its own shader instead (ao_bitmask_blur.hlsli,
// line_sweep_ao.hpp), so Game's ambient occlusion costs what it did. Include after the game's shared.h.
cbuffer camera : register(b0) {
  float g_vertical_fov : packoffset(c26.w);
}

Texture2D<float> gbuffer_depth_texture : register(t0);
Texture2D<float> depth_texture : register(t1);  // the game's name: the collect's AO
RWTexture2D<unorm float> t_rw_ssao : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 coords : SV_DispatchThreadID) {
  uint2 size;
  t_rw_ssao.GetDimensions(size.x, size.y);
  if (any(coords.xy >= size)) return;
  const float center_depth = gbuffer_depth_texture.Load(int3(coords.xy, 0));
  const uint2 last = size - 1u;
  const float tolerance = center_depth * g_vertical_fov;
  float occlusion = 0.f;
  float weight = 0.f;
  for (int x = -5; x <= 5; ++x) {
    // Unsigned, as the game's: left of the screen wraps around and clamps to the right edge.
    const uint2 position = min(last, uint2(coords.x + uint(x * 5), coords.y));
    const float depth = gbuffer_depth_texture.Load(int3(position, 0));
    const float tap_weight = 1.f - saturate((abs(depth - center_depth) - tolerance * 0.008f) / (tolerance * 0.001f));
#if LINE_SWEEP_HALF_SIZE_AO
    occlusion += depth_texture.Load(int3(position >> 1u, 0)) * tap_weight;
#else
    occlusion += depth_texture.Load(int3(position * 2u, 0)) * tap_weight;
#endif
    weight += tap_weight;
  }
  t_rw_ssao[coords.xy] = occlusion / weight;
}

#endif  // SRC_TWWARHAMMER2_LINE_SWEEP_BLUR_HLSLI_
