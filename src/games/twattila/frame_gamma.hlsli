#ifndef SRC_TWATTILA_FRAME_GAMMA_HLSLI_
#define SRC_TWATTILA_FRAME_GAMMA_HLSLI_

// The frame's gamma (TW_FRAME_GAMMA in the game's shared.h: Attila's, Thrones of Britannia's): what the swap chain proxy
// decodes the frame with, so what every pass that writes it at Paper White encodes with. Each gamma a constant of its
// own: fxc makes 2.0's a square root, so with Gamma Correction off the frame keeps its bits.
float3 EncodeFrame(float3 color) {
  if (TW_FRAME_GAMMA == 2.f) return renodx::color::gamma::EncodeSafe(color, 2.f);
  return renodx::color::gamma::EncodeSafe(color, 2.2f);
}

float3 DecodeFrame(float3 color) {
  if (TW_FRAME_GAMMA == 2.f) return renodx::color::gamma::DecodeSafe(color, 2.f);
  return renodx::color::gamma::DecodeSafe(color, 2.2f);
}

// The HDR tone mappers' output as the frame's 2.2 decode shows the game's gamma 2.0 picture (x^1.1 per BT.709 channel),
// within Peak: their Peak is pre-corrected for grey (TW_TONE_MAP_PEAK), but the power lifts a saturated highlight's
// BT.2020 channels up to 4% past it; those move toward their luminance until they fit (as Shogun 2 after its
// ApplyGameGamma).
float3 EmulateFrameGamma(float3 color, float peak) {
  const float3 shown = DecodeFrame(renodx::color::gamma::EncodeSafe(color, 2.f));
  float3 color_bt2020 = max(renodx::color::bt2020::from::BT709(shown), 0.f);
  const float y = min(renodx::color::y::from::BT2020(color_bt2020), peak);
  const float channel_max = max(color_bt2020.r, max(color_bt2020.g, color_bt2020.b));
  [branch]
  if (channel_max > peak) {
    color_bt2020 = y + (color_bt2020 - y) * saturate((peak - y) / (channel_max - y));
  }
  return renodx::color::bt709::from::BT2020(color_bt2020);
}

#endif  // SRC_TWATTILA_FRAME_GAMMA_HLSLI_
