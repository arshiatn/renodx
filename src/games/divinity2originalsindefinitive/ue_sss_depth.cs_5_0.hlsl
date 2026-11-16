/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Bend's screen-space shadows (screen_space.hpp, named as talesofarise_dlss/ue_screen_space_shadows.hpp embeds it): the
// G-buffer pass's D24S8 depth (dlaa.hpp keeps it at LinearizeDepth), already the standard depth (near 0, far 1) Bend's
// shaders take (twattila/sss_bend, sss_blur), copied into their single-channel texture.
Texture2D<float> t_scene_depth : register(t0);
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  t_rw_depth.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  t_rw_depth[id.xy] = t_scene_depth.Load(int3(id.xy, 0));
}
