#ifndef SRC_TWATTILA_LENSFLARE_HLSLI_
#define SRC_TWATTILA_LENSFLARE_HLSLI_

// lenseflare.hlsl ps30_flare_0 - 3 (lensflare00 - 03): Lens Flare slider in HDR, Paper White relative to UI.
#include "./shared.h"

float4 ApplyAttilaLensFlare(float4 stock_color)
{
  // Restore the UNORM clamp lost to the FP16 upgrade.
  float4 flare = saturate(stock_color);

  // Made for a 2.2 screen (ignores in-game gamma); the frame is 2.0. Scale in linear.
  float3 flare_linear = renodx::color::gamma::DecodeSafe(flare.xyz);
  flare_linear *= (HDR >= 1.f ? SI.lensflare : 1.f)
                * ATTILA_PAPER_WHITE;
  flare.xyz = renodx::color::gamma::EncodeSafe(flare_linear, 2.0f);

  return flare;
}

#endif  // SRC_TWATTILA_LENSFLARE_HLSLI_
