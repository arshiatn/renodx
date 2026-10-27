#ifndef SRC_TWATTILA_BLOOM_HLSLI_
#define SRC_TWATTILA_BLOOM_HLSLI_

#include "./shared.h"

// hdr_tone_mapper's high pass (bloom source, both cutoffs): Attila caps every pixel's bloom at luminance 25, invisible
// in SDR (the frame clips) but in HDR every bright source gets the same flat halo. HDR: stock up to 25, then a
// logarithmic shoulder (100 -> 75, 1000 -> 158), so brighter sources keep brighter bloom without fireflies.
float3 CapBloomSource(float3 bloom_source) {
  float luminance = dot(bloom_source, float3(0.212599993f, 0.715200007f, 0.0722000003f));
  if (luminance <= 25.f) return bloom_source;
  float capped = (HDR >= 1.f ? 25.f + 25.f * log2(luminance / 25.f) : 25.f);
  return bloom_source * (capped / luminance);
}

// The game's high pass of one source pixel (its luminance in alpha) above `cutoff` (the auto exposure's or the constant
// tone map's threshold).
float4 BloomHighPassTap(float4 source, float cutoff) {
  const float3 bloom = max(0.f, CapBloomSource(source.rgb * source.a / cutoff - source.rgb));
  return float4((source.a >= cutoff) ? bloom : 0.f, 1.f);
}

#include "./bloom_filter.hlsli"

#endif  // SRC_TWATTILA_BLOOM_HLSLI_
