/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_BLOOM_FILTER_HLSLI_
#define SRC_GAMES_TWATTILA_BLOOM_FILTER_HLSLI_

// Smoother bloom for the Warscape games' hdr_tone_mapper (the game's bloom: a high pass into a smaller target, blurred,
// added by the tone mapper or Troy's / Pharaoh's hdr_to_screen as its glow): the stock high pass takes one tap per bloom
// texel (small bright sources there or not: blocks that flicker as the camera moves) and the tone mapper one bilinear tap
// of the small texture (its texels as blocks and diamonds around bright sources, more so in HDR). The includer (its
// bloom.hlsli, or bloom_high_pass.hlsli) defines first:
//   float4 BloomHighPassTap(float4 source, float cutoff)  its game's high pass of one source pixel
// or BLOOM_SAMPLE_ONLY (the tone mappers: SampleBloomSmooth only).

#ifndef BLOOM_SAMPLE_ONLY
// The high pass of every source pixel the bloom texel at `target_pixel` (SV_Position plus the game's texel offset)
// covers, averaged (at most 8 x 8; one where the target is the source's size: the stock tap).
float4 BloomHighPassAveraged(Texture2D<float4> source, float2 target_pixel, float2 target_recip_size, float cutoff) {
  uint2 size;
  source.GetDimensions(size.x, size.y);
  const float2 ratio = size * target_recip_size;
  const int2 count = clamp(int2(round(ratio)), 1, 8);
  const int2 first = int2(floor(target_pixel * ratio - 0.5f * count + 0.5f));
  float4 sum = 0.f;
  [loop]
  for (int y = 0; y < count.y; ++y) {
    [loop]
    for (int x = 0; x < count.x; ++x) {
      sum += BloomHighPassTap(source.Load(int3(clamp(first + int2(x, y), 0, int2(size) - 1), 0)), cutoff);
    }
  }
  return sum / (count.x * count.y);
}
#endif

// The bloom at `uv` (the screen's), a cubic B-spline over the 4 x 4 texels around it: smooth at any scale.
float4 SampleBloomSmooth(Texture2D<float4> bloom, float2 uv) {
  uint2 size;
  bloom.GetDimensions(size.x, size.y);
  const float2 position = uv * size - 0.5f;
  const float2 base = floor(position);
  const float2 f = position - base;
  const float2 f2 = f * f;
  const float2 f3 = f2 * f;
  const float2 weights[4] = {
      (1.f - 3.f * f + 3.f * f2 - f3) / 6.f,
      (4.f - 6.f * f2 + 3.f * f3) / 6.f,
      (1.f + 3.f * f + 3.f * f2 - 3.f * f3) / 6.f,
      f3 / 6.f,
  };
  float4 sum = 0.f;
  [unroll]
  for (int y = 0; y < 4; ++y) {
    [unroll]
    for (int x = 0; x < 4; ++x) {
      const int2 texel = clamp(int2(base) + int2(x - 1, y - 1), 0, int2(size) - 1);
      sum += weights[x].x * weights[y].y * bloom.Load(int3(texel, 0));
    }
  }
  return sum;
}

#endif  // SRC_GAMES_TWATTILA_BLOOM_FILTER_HLSLI_
