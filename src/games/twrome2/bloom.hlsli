#ifndef SRC_TWROME2_BLOOM_HLSLI_
#define SRC_TWROME2_BLOOM_HLSLI_

#include "./shared.h"

// Rome II's high pass caps the bloom source's luminance at 2.1 x the white point (xyY): invisible in SDR, a flat halo on
// every bright source in HDR. HDR: a logarithmic shoulder above it (Attila's bloom fix).
float CapBloomLuminance(float luminance, float cap) {
  if (!(luminance > cap)) return luminance;
  return (HDR >= 1.f ? cap + cap * log2(luminance / cap) : cap);
}

// The game's high pass of one source pixel (xyY: its luminance above 1.5 x `cutoff`, the white point of the auto
// exposure or the constant tone map, capped at 2.1 x it; its chromaticity kept).
float4 BloomHighPassTap(float4 source, float cutoff) {
  const float3 xyz = float3(dot(float3(0.412400007f, 0.357600003f, 0.180500001f), source.rgb),
                            dot(float3(0.212599993f, 0.715200007f, 0.0722000003f), source.rgb),
                            dot(float3(0.0193000007f, 0.119199999f, 0.950500011f), source.rgb));
  if (!(xyz.y >= 1.5f * cutoff)) return float4(0.f, 0.f, 0.f, 1.f);
  const float sum = max(0.00100000005f, xyz.x + xyz.y + xyz.z);
  const float chroma_x = xyz.x / sum;
  const float chroma_y = xyz.y / sum;
  const float luminance = CapBloomLuminance(xyz.y, 2.0999999f * cutoff) - 1.5f * cutoff;
  const float3 bloom_xyz = float3(chroma_x, max(0.00100000005f, chroma_y), 1.f - chroma_x - chroma_y)
                           * (luminance / max(0.00100000005f, chroma_y));
  const float3 bloom = float3(dot(float3(3.24049997f, -1.53719997f, -0.49849999f), bloom_xyz),
                              dot(float3(-0.969299972f, 1.87600005f, 0.0416000001f), bloom_xyz),
                              dot(float3(0.0555999987f, -0.203999996f, 1.05719995f), bloom_xyz));
  return float4(max(0.f, bloom), source.a);
}

#include "../twattila/bloom_filter.hlsli"

#endif  // SRC_TWROME2_BLOOM_HLSLI_
