/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private debug capture (Advanced button), not a game replacement registration: a view's values x Scale + Bias,
// saturated, ^Gamma, into an 8-bit copy saved as PNG. Drawn with the addon's full-screen triangle (0x52594504).
cbuffer CaptureConstants : register(b12) {
  float Scale;
  float Bias;
  float Gamma;
}

Texture2D<float4> Source : register(t0);

float4 main(float4 v0 : SV_Position0) : SV_Target0 {
  return float4(pow(saturate(Source.Load(int3(v0.xy, 0)).rgb * Scale + Bias), Gamma), 1.f);
}
