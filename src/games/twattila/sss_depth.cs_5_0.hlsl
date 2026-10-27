/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (screen_space_shadows.hpp): the scene's depth (a copy of the depth-stencil buffer, or the
// G-buffer's channel 4; its x; with the game's MSAA its sample 0, SSS_DEPTH_MS) into a single-channel texture Bend's
// sampler reads.
#ifdef SSS_DEPTH_MS
Texture2DMS<float4> t_scene_depth : register(t0);
#else
Texture2D<float4> t_scene_depth : register(t0);
#endif
RWTexture2D<float> t_rw_depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 coords : SV_DispatchThreadID)
{
#ifdef SSS_DEPTH_MS
  t_rw_depth[coords.xy] = t_scene_depth.Load(int2(coords.xy), 0).x;
#else
  t_rw_depth[coords.xy] = t_scene_depth.Load(int3(coords.xy, 0)).x;
#endif
}
