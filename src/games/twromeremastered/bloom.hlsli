#ifndef SRC_TWROMEREMASTERED_BLOOM_HLSLI_
#define SRC_TWROMEREMASTERED_BLOOM_HLSLI_

#include "./shared.h"

// Rome Remastered's bloom threshold caps the brightest channel at `cap`: invisible in SDR, a flat halo on every bright
// source in HDR. HDR: a logarithmic shoulder above it (Attila's bloom fix).
float3 CapBloomChannels(float3 bloom_source, float cap) {
  float brightest = max(bloom_source.r, max(bloom_source.g, bloom_source.b));
  if (!(brightest > cap)) return bloom_source;
  float capped = (HDR >= 1.f ? cap + cap * log2(brightest / cap) : cap);
  return bloom_source * (capped / brightest);
}

#endif  // SRC_TWROMEREMASTERED_BLOOM_HLSLI_
