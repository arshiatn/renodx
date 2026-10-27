/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Attila ray tracing (rt_scene.hpp): the sun light's occlusion for the patched sun shaders (t49): the traced sun's
// (1 - its visibility, at the render size's pixel) and the screen-space shadow's mask (t1, unbound: 0), the darker.
Texture2D<float> visibility : register(t0);
Texture2D<float> screen_space : register(t1);
RWTexture2D<unorm float> occlusion : register(u0);

cbuffer rt_occlusion : register(b0)
{
  uint4 size;  // xy the render size
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  uint2 target;
  occlusion.GetDimensions(target.x, target.y);
  if (any(id.xy >= target)) return;
  const float traced = all(id.xy < size.xy) ? 1.f - visibility[id.xy] : 0.f;
  occlusion[id.xy] = max(traced, screen_space[id.xy]);
}
