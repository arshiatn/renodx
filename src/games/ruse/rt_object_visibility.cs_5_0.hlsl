/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced object shadows (ray_tracing.hpp): the helper's sun visibility (R8, the bridge's top-left) into the RGBA16F
// texture the object shaders read at s15: x the visibility, alpha 1 where they take it in place of their shadow-map
// factor (patched_shaders.h: `cmp rD, -rT.w, rD, rT.x`). With the rays replacing the maps everywhere (`blocked` past 1);
// else only where the rays found the sun blocked (below `blocked`), the game's factor elsewhere: no shadow of its maps
// is lost, the rays add the ones the maps miss.
Texture2D<float> visibility : register(t0);
RWTexture2D<float4> object_visibility : register(u0);

cbuffer object_settings : register(b0) {
  float4 blocked;  // x: the visibility below which the rays are taken
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width;
  uint height;
  object_visibility.GetDimensions(width, height);
  if (any(id.xy >= uint2(width, height))) return;
  const float sun = visibility[id.xy];
  object_visibility[id.xy] = float4(sun, sun, sun, (sun < blocked.x) ? 1.f : 0.f);
}
