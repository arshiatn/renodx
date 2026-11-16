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

#endif  // SRC_TWATTILA_FRAME_GAMMA_HLSLI_
