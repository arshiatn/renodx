/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Mip step (79F74068) for a picture filling a part of its target (render below 100%): reads stop at the picture's share of this
// level, so the level keeps nothing from the unused rest. The rest repeats the edge: the resolve filters across it.
// At a share of 1 the game's result.
Texture2D<float4> t_in_convolution : register(t0);
RWTexture2D<float4> t_out_convolution : register(u0);

cbuffer SsrPictureShare : register(b12) {
  float2 picture_share;
};

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID) {
  uint width, height;
  t_in_convolution.GetDimensions(width, height);
  const int2 last = (int2)ceil(picture_share * float2(width, height)) - 1;
  const int2 origin = (int2)vThreadID.xy * 2;
  float4 sum = t_in_convolution.Load(int3(min(origin, last), 0));
  sum = t_in_convolution.Load(int3(min(origin + int2(0, 1), last), 0)) + sum;
  sum = t_in_convolution.Load(int3(min(origin + int2(1, 0), last), 0)) + sum;
  sum = t_in_convolution.Load(int3(min(origin + int2(1, 1), last), 0)) + sum;
  t_out_convolution[vThreadID.xy] = 0.25f * sum;
}
