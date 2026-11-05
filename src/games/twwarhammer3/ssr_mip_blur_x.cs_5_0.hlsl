/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Blur X (66C301FF) for a picture filling a part of its target (render below 100%): reads stop at the picture's share of this
// level, so the level keeps nothing from the unused rest. The rest repeats the edge: the resolve filters across it.
// At a share of 1 the game's result.
Texture2D<float4> t_in_convolution : register(t0);
RWTexture2D<float4> t_out_convolution : register(u0);

cbuffer SsrPictureShare : register(b12) {
  float2 picture_share;
};

static const float WEIGHTS[7] = {0.005980f, 0.060626f, 0.241843f, 0.383103f, 0.241843f, 0.060626f, 0.005980f};

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID) {
  uint width, height;
  t_out_convolution.GetDimensions(width, height);
  const int2 last = (int2)ceil(picture_share * float2(width, height)) - 2;
  float4 sum = 0.f;
  for (int tap = 0; tap < 7; ++tap) {
    const int2 place = min((int2)vThreadID.xy + int2(tap - 3, 0), last);
    sum = t_in_convolution.Load(int3(max(int2(0, 0), place), 0)) * WEIGHTS[tap] + sum;
  }
  t_out_convolution[vThreadID.xy] = sum;
}
