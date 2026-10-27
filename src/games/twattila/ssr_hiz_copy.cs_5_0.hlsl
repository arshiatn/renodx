/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The reflections' depth pyramid (ssr_hiz.hpp), its first level: the scene's depth (t0) as it is.
Texture2D<float> t_depth : register(t0);
RWTexture2D<float> t_rw_level : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  uint width, height;
  t_rw_level.GetDimensions(width, height);
  if (any(pixel >= uint2(width, height))) return;
  t_rw_level[pixel] = t_depth.Load(int3(pixel, 0));
}
