#ifndef SRC_TWWARHAMMER2_LENSFLARE_HLSLI_
#define SRC_TWWARHAMMER2_LENSFLARE_HLSLI_

// lenseflare.hlsl ps40_flare_0 - 3 (lensflare00 - 03, sm_5_0), drawn on the frame (in its gamma): Lens Flare slider in HDR,
// Paper White relative to UI (Rome II's).
#include "./shared.h"

float4 ApplyWarhammerLensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // The game's own add, scaled in linear with the frame's gamma (the swap chain proxy's decode): at Lens Flare 1 and
  // Paper White = UI it is the game's.
  flare.xyz = EncodeFrame(DecodeFrame(flare.xyz) * (SI.lensflare * (SI.diffuse_white_nits / SI.graphics_white_nits)));

  return flare;
}

#endif  // SRC_TWWARHAMMER2_LENSFLARE_HLSLI_
