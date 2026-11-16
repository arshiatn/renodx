// Pharaoh's lens overlays (lenseflare.hlsl: the flares, the blood on the lens), drawn after the tone map into the
// upgraded FP16 frame. Stock colour in, final colour out.
#include "./shared.h"

float4 ApplyPharaohLensOverlay(float4 stock_color, float scale)
{
  // Restores the write clamp the upgraded FP16 target no longer provides.
  float4 overlay = saturate(stock_color);

  if (HDR >= 0.5f)
  {
    // Scale in linear. The frame is in its gamma (t01-t04, the swapchain proxy's decode:
    // twattila/frame_gamma.hlsli). The ratio is the one t01 applies to the scene.
    overlay.xyz = EncodeFrame(DecodeFrame(overlay.xyz) * (scale * (SI.diffuse_white_nits / SI.graphics_white_nits)));
  }

  return overlay;
}
