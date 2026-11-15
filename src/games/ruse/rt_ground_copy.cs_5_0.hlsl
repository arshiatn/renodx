/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced ground shadows (ray_tracing.hpp): the helper's sun visibility (R8, the bridge's top-left) into the texture
// R.U.S.E.'s D3D9 device shares (R32F).
Texture2D<float> visibility : register(t0);
RWTexture2D<float> shared_visibility : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  shared_visibility.GetDimensions(width, height);
  if (any(id.xy >= uint2(width, height))) return;
  shared_visibility[id.xy] = visibility[id.xy];
}
