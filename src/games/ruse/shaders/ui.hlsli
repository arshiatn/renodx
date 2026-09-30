#ifndef SRC_RUSE_UI_HLSLI_
#define SRC_RUSE_UI_HLSLI_

#include "../shared.h"

float4 RuseScaleUI(float4 color) {
  if (SI.peak_white_nits > 0.f && SI.ui_draw > 0.5f) {
    // These shaders were authored for UNORM. Bound their native output and
    // alpha before applying brightness in the floating-point backbuffer.
    color = saturate(color);
    color.rgb *= SI.ui_scale;
  }
  return color;
}

#endif  // SRC_RUSE_UI_HLSLI_
