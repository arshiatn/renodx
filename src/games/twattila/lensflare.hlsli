#ifndef SRC_TWATTILA_LENSFLARE_HLSLI_
#define SRC_TWATTILA_LENSFLARE_HLSLI_

// lenseflare.hlsl ps30_flare_0 - 3 (lensflare00 - 03): Lens Flare slider, Paper White relative to UI.
#include "./shared.h"

float4 ApplyAttilaLensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // The game's own add, scaled in linear with the frame's gamma (the swap chain proxy's decode): at Lens Flare 1 and
  // Paper White = UI it is the game's.
  flare.xyz = EncodeFrame(DecodeFrame(flare.xyz) * (SI.lensflare * ATTILA_PAPER_WHITE));

  return flare;
}

#endif  // SRC_TWATTILA_LENSFLARE_HLSLI_
