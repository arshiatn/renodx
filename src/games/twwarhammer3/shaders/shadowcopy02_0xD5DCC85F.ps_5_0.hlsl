/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#include "./shadowcopy.hlsli"

Texture2DArray<float> t_dynamic_objects_shadowmap_array : register(t0);

float main(float4 position : SV_Position) : SV_Depth {
  const int texels = ShadowCopyTexels(t_dynamic_objects_shadowmap_array);
  const int2 first = ShadowCopyFirst(position.xy, texels);
  float deepest = 0.f;
  for (int y = 0; y < texels; ++y) {
    for (int x = 0; x < texels; ++x) {
      deepest = max(deepest, t_dynamic_objects_shadowmap_array.Load(int4(first + int2(x, y), texture_array_index, 0)));
    }
  }
  return deepest;
}
