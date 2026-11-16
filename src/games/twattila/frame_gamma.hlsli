#ifndef SRC_TWATTILA_FRAME_GAMMA_HLSLI_
#define SRC_TWATTILA_FRAME_GAMMA_HLSLI_

// The frame's gamma (Attila, Thrones of Britannia, Rome II, Warhammer I / II / III, Troy, Pharaoh: their shared.h
// includes this after SI, HDR and HDR_PEAK), the swap chain proxy's decode: Gamma Correction on, 2.2, the game's gamma
// 2.0 picture as a 2.2 display shows it (x^1.1), in every Rendering Mode; off, 2.0. Rendering Mode SDR on an SDR display
// the other way round: off hands the game's picture to the display as it is.
// C++ includes this file after its shared.h too (SI the addon's shader_injection, HDR its Rendering Mode): the add-on's
// own passes that decode or encode the frame (an upscaler's prepare and finish) take the same gamma in their constants.
#define TW_FRAME_GAMMA (((SI.gamma_correction != 0.f) != (SI.swap_chain_encoding == 2.f && HDR < 0.5f)) ? 2.2f : 2.f)

#ifndef __cplusplus
// The HDR tone mappers' Peak, pre-corrected for the frame's 2.2 decode of their gamma 2.0 output: their top still lands
// on Peak.
#define TW_TONE_MAP_PEAK ((TW_FRAME_GAMMA == 2.f) ? HDR_PEAK : pow(HDR_PEAK, 2.f / TW_FRAME_GAMMA))

// What every pass that writes the frame at Paper White encodes with. Each gamma a constant of its own: fxc makes 2.0's a
// square root, so with Gamma Correction off the frame keeps its bits.
float3 EncodeFrame(float3 color) {
  if (TW_FRAME_GAMMA == 2.f) return renodx::color::gamma::EncodeSafe(color, 2.f);
  return renodx::color::gamma::EncodeSafe(color, 2.2f);
}

float3 DecodeFrame(float3 color) {
  if (TW_FRAME_GAMMA == 2.f) return renodx::color::gamma::DecodeSafe(color, 2.f);
  return renodx::color::gamma::DecodeSafe(color, 2.2f);
}

// Rendering Mode SDR: the game's picture (encoded at its in-game Gamma, stock_gamma) in the frame at Paper White. Gamma
// Correction starts from gamma 2.0 whatever the in-game Gamma (the frame's 2.2 decode shows it as a 2.2 display does);
// off, the in-game Gamma stays.
float3 SdrToFrame(float3 stock, float stock_gamma, float paper_white) {
  [branch]
  if (SI.gamma_correction != 0.f) {
    stock = renodx::color::gamma::EncodeSafe(renodx::color::gamma::DecodeSafe(stock, stock_gamma), 2.f);
  }
  return EncodeFrame(DecodeFrame(stock) * paper_white);
}

// The HDR tone mappers' output (their Peak TW_TONE_MAP_PEAK) in the frame at Paper White. Gamma Correction: as the
// frame's 2.2 decode shows the game's gamma 2.0 picture (x^1.1 per BT.709 channel), within Peak: their Peak is
// pre-corrected for grey, but the power lifts a saturated highlight's BT.2020 channels up to 4% past it; those move toward
// their luminance until they fit (as Shogun 2 after its ApplyGameGamma).
float3 HdrToFrame(float3 color, float paper_white) {
  [branch]
  if (TW_FRAME_GAMMA != 2.f) {
    const float3 shown = DecodeFrame(renodx::color::gamma::EncodeSafe(color, 2.f));
    float3 color_bt2020 = max(renodx::color::bt2020::from::BT709(shown), 0.f);
    const float y = min(renodx::color::y::from::BT2020(color_bt2020), HDR_PEAK);
    const float channel_max = max(color_bt2020.r, max(color_bt2020.g, color_bt2020.b));
    [branch]
    if (channel_max > HDR_PEAK) {
      color_bt2020 = y + (color_bt2020 - y) * saturate((HDR_PEAK - y) / (channel_max - y));
    }
    color = renodx::color::bt709::from::BT2020(color_bt2020);
  }
  return EncodeFrame(color * paper_white);
}
#endif  // __cplusplus

#endif  // SRC_TWATTILA_FRAME_GAMMA_HLSLI_
