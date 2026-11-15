#ifndef SRC_RUSE_UI_HLSLI_
#define SRC_RUSE_UI_HLSLI_

#include "../shared.h"
// twattila/autohdr.hlsli with R.U.S.E.'s shared.h in place of Attila's (it needs HDR, HDR_PEAK and renodx only).
#define SRC_TEMPLATE_SHARED_H_
#include "../../twattila/autohdr.hlsli"

float4 RuseScaleUI(float4 color) {
  if (SI.peak_white_nits > 0.f && SI.ui_draw > 0.5f) {
    // These shaders were authored for UNORM. Bound their native output and
    // alpha before applying brightness in the floating-point backbuffer.
    color = saturate(color);
    color.rgb *= SI.ui_scale;
  }
  return color;
}

// The final video / loading-screen draw: Paper White, and Video AutoHDR (above mid gray toward Peak) after 1 code value
// of interleaved gradient noise when Video Dithering is on, static in screen space (`vpos`).
float4 RuseScaleVideo(float4 color, float2 vpos) {
  if (SI.peak_white_nits > 0.f && SI.diffuse_white_nits > 0.f && SI.ui_draw > 0.5f) {
    // Restore native UNORM clipping before scaling the final video draw.
    color = saturate(color);
    [branch]
    if (HDR >= 1.f && SI.video_autohdr > 0.f) {
      if (SI.video_dithering != 0.f) {
        color.rgb += (frac(52.9829189f * frac(dot(floor(vpos), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f;
      }
      color.rgb = renodx::color::gamma::EncodeSafe(AutoHdrHighlights(renodx::color::gamma::DecodeSafe(color.rgb), SI.video_autohdr));
    }
    color.rgb *= SI.video_scale;
  }
  return color;
}

#endif  // SRC_RUSE_UI_HLSLI_
