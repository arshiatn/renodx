/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Hierarchical depth step (CB0A1AF6: nearest of 4 in x, farthest in y) for a picture filling a part of its target (render below
// 100%): reads stop at the picture's share of this level, so a cell on the picture's edge holds only the picture's depths. At a
// share of 1 the game's result. The game's two branches for threads past x 9999 never run at these sizes and are left out.
Texture2D<float4> t_depth_buffer : register(t0);
RWTexture2D<float4> t_out_depth : register(u0);

cbuffer SsrPictureShare : register(b12) {
  float2 picture_share;
};

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID) {
  uint width, height;
  t_depth_buffer.GetDimensions(width, height);
  const int2 last = (int2)ceil(picture_share * float2(width, height)) - 1;
  const int2 origin = (int2)vThreadID.xy * 2;
  const float2 right = t_depth_buffer.Load(int3(min(origin + int2(1, 0), last), 0)).xy;
  const float2 below = t_depth_buffer.Load(int3(min(origin + int2(0, 1), last), 0)).xy;
  const float2 corner = t_depth_buffer.Load(int3(min(origin, last), 0)).xy;
  const float2 diagonal = t_depth_buffer.Load(int3(min(origin + int2(1, 1), last), 0)).xy;
  const float nearest = min(min(below.x, diagonal.x), min(right.x, corner.x));
  const float farthest = max(max(below.y, diagonal.y), max(right.y, corner.y));
  t_out_depth[vThreadID.xy] = float4(nearest, farthest, nearest, nearest);
}
