/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_AO_UPSAMPLE_HLSLI_
#define SRC_GAMES_TWATTILA_AO_UPSAMPLE_HLSLI_

// Ambient Occlusion at a lower resolution (ao_pass.hpp; `scale` the game's size over the results', e.g. 2 at Half): a
// pixel's result from the 3 x 3 nearest results (`half_results`: each the result and the distance, or depth, of the pixel
// it stands for, at its texel x scale; 0: nothing there),
// by how far their pixel is and how close their distance is to this pixel's (`distance`), so no edge blends into the
// surface behind it (as the Light Bounce's apply). None within 3% (far ground seen at a grazing angle, a thin object
// only this pixel sees): the one closest in distance; none at all: open.
float AoUpsample(Texture2D<float2> half_results, int2 pixel, float distance, float scale) {
  const int2 center = int2(floor(float2(pixel) / scale));
  float2 sum = 0.f;
  float closest = 1.f;
  float closest_difference = 1e30f;
  [unroll]
  for (int y = -1; y <= 1; ++y) {
    [unroll]
    for (int x = -1; x <= 1; ++x) {
      const int2 texel = center + int2(x, y);
      const float2 result = half_results.Load(int3(texel, 0));
      if (!(result.y > 0.f)) continue;
      // Its pixel from this one, in result texels.
      const float2 offset = (float2(texel) * scale - float2(pixel)) / scale;
      const float near = exp2(-0.25f * dot(offset, offset));
      const float difference = abs(result.y - distance);
      sum += float2(result.x, 1.f) * (near * saturate(1.f - difference / (0.03f * distance)));
      [flatten]
      if (difference < closest_difference) {
        closest = result.x;
        closest_difference = difference;
      }
    }
  }
  return (sum.y > 0.f) ? (sum.x / sum.y) : closest;
}

#endif  // SRC_GAMES_TWATTILA_AO_UPSAMPLE_HLSLI_
