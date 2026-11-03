#ifndef SRC_TWWARHAMMER3_LENSFLARE_HLSLI_
#define SRC_TWWARHAMMER3_LENSFLARE_HLSLI_

#include "./shared.h"

// Lens overlays drawn on the frame after t00 (flares, blood splatter). Stock colour in, final colour out.
float4 ScaleLensOverlay(float4 stock_color, float strength)
{
  // UNORM clamp the FP16 target lost.
  float4 flare = saturate(stock_color);

  // Frame is 2.2-encoded here. Scale in linear, same ratio t00 uses.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= strength
                * (SI.diffuse_white_nits / SI.graphics_white_nits);
  flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear);

  return flare;
}

// lensflare00-03: Lens Flare slider.
float4 ApplyWH3LensFlare(float4 stock_color)
{
  return ScaleLensOverlay(stock_color, SI.lensflare);
}

#endif  // SRC_TWWARHAMMER3_LENSFLARE_HLSLI_
