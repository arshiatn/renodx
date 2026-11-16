#ifndef SRC_TWWARHAMMER3_LENSFLARE_HLSLI_
#define SRC_TWWARHAMMER3_LENSFLARE_HLSLI_

#include "./shared.h"

// Lens overlays drawn on the frame after t00 (flares, blood splatter). Stock colour in, final colour out.
float4 ScaleLensOverlay(float4 stock_color, float strength)
{
  // UNORM clamp the FP16 target lost.
  float4 flare = saturate(stock_color);

  // The game's own add, scaled in linear with the frame's gamma (the swap chain proxy's decode, twattila/frame_gamma.hlsli),
  // the ratio t00 uses: at strength 1 and Paper White = UI it is the game's.
  flare.xyz = EncodeFrame(DecodeFrame(flare.xyz) * (strength * (SI.diffuse_white_nits / SI.graphics_white_nits)));

  return flare;
}

// lensflare00-03: Lens Flare slider.
float4 ApplyWH3LensFlare(float4 stock_color)
{
  return ScaleLensOverlay(stock_color, SI.lensflare);
}

#endif  // SRC_TWWARHAMMER3_LENSFLARE_HLSLI_
