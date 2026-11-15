/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private DLSS output copy into the game's TAA target, not a game replacement registration.
// Encoded like the game's TAA output (sRGB, or the gamma 2.2 round trip of the preparation pass).
cbuffer PassConstants : register(b12) {
  uint ColorEncoding;
}

Texture2D<float4> DlssOutput : register(t0);

// Only SV_Position: it links with any of the game's fullscreen vertex shaders.
float4 main(float4 v0 : SV_Position0) : SV_Target0 {
  float3 color = DlssOutput.Load(int3(v0.xy, 0)).rgb;
  if (ColorEncoding != 0) {
    return float4(sign(color) * pow(abs(color), 1.f / 2.2f), 0.f);
  }
  // The game's sRGB encode (TAA constants), into an 8-bit target.
  color = max(color, 0.f);
  const float3 encoded = (color < 0.00313080009f) ? (12.9200001f * color)
                                                  : (exp2(log2(color) * 0.416666657f) * 1.05499995f - 0.0549999997f);
  return float4(encoded, 0.f);
}
