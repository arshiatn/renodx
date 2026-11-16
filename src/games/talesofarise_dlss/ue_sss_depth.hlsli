/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows on an Unreal game (ue_screen_space_shadows.hpp): its scene depth (device Z, reversed: the sky 0)
// as 1 - device Z, the standard depth (near 0, far 1) Bend's shaders take (twattila/sss_bend, sss_blur); its far depth
// at the depth's size. Game-neutral.
Texture2D<float> t_scene_depth : register(t0);
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  t_rw_depth.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  t_rw_depth[id.xy] = 1.f - t_scene_depth.Load(int3(id.xy, 0));
}
