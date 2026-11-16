/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space ground shadows (ray_tracing.hpp G2): Bend's occlusion (1 - its shadow term) into the shared sun
// visibility by minimum, over the rays' (or 1 without them): the darker of the two.
Texture2D<float> occlusion : register(t0);
RWTexture2D<float> shared_visibility : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  shared_visibility.GetDimensions(width, height);
  if (any(id.xy >= uint2(width, height))) return;
  shared_visibility[id.xy] = min(shared_visibility[id.xy], 1.f - occlusion[id.xy]);
}
