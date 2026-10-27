/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray tracing (ray_tracing.hpp KeepColor): the game's HDR scene (R11G11B10, as it copies it at the end of the frame for
// next frame's screen-space reflections) into the helper's colour (RGBA16F): last frame's light for the next frame's
// reflections and paths.
Texture2D<float3> scene : register(t0);
RWTexture2D<float4> color : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint width, height;
  scene.GetDimensions(width, height);
  if (id.x >= width || id.y >= height) return;
  color[id.xy] = float4(scene[id.xy], 1.f);
}
