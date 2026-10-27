#ifndef SRC_TWWARHAMMER2_LENSFLARE_HLSLI_
#define SRC_TWWARHAMMER2_LENSFLARE_HLSLI_

// lenseflare.hlsl ps40_flare_0 - 3 (lensflare00 - 03, sm_5_0), drawn on the 2.2-encoded frame: Lens Flare slider in HDR,
// Paper White relative to UI (Rome II's).
#include "./shared.h"

float4 ApplyWarhammerLensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // Frame is gamma-encoded here; scale in linear at Paper White relative to UI.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= (HDR >= 1.f ? SI.lensflare : 1.f)
                * (SI.diffuse_white_nits / SI.graphics_white_nits);
  flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear);

  return flare;
}

#endif  // SRC_TWWARHAMMER2_LENSFLARE_HLSLI_
