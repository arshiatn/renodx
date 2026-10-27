/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The reflections' depth pyramid (ssr_hiz.hpp), each further level: the nearest depth (the least: 0 near, 1 far) of the
// texels of the level before (t0) it covers, 2 x 2, and 3 at an odd size's last row or column (none left out).
Texture2D<float> t_previous : register(t0);
RWTexture2D<float> t_rw_level : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 pixel : SV_DispatchThreadID)
{
  uint width, height;
  t_rw_level.GetDimensions(width, height);
  if (any(pixel >= uint2(width, height))) return;
  uint previous_width, previous_height;
  t_previous.GetDimensions(previous_width, previous_height);
  const int2 last = int2(previous_width, previous_height) - 1;
  const int2 extra = int2((pixel.x == width - 1 && (previous_width & 1) != 0) ? 2 : 1,
                          (pixel.y == height - 1 && (previous_height & 1) != 0) ? 2 : 1);
  float nearest = 1.f;
  for (int y = 0; y <= extra.y; ++y)
  {
    for (int x = 0; x <= extra.x; ++x)
    {
      nearest = min(nearest, t_previous.Load(int3(min(int2(pixel) * 2 + int2(x, y), last), 0)));
    }
  }
  t_rw_level[pixel] = nearest;
}
