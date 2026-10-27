/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ambient Occlusion: Visibility Bitmask in place of Warhammer II's collect (line_sweep_ao.hpp dispatches it, 8 x 8 groups
// over the collect's output at u0, with this frame's depth at t2, the sweep's camera at b0 and the addon's settings at
// b13): twwarhammer3/line_sweep_bitmask.hlsli. The game's blur, when it runs, blurs it as the game's. With
// AO_UPSAMPLE (ao_upsample.cs_5_0.hlsl): Half resolution's upsample.
#include "./shared.h"

cbuffer ao_camera : register(b0) {
  float4x4 ao_inv_projection : packoffset(c17);
}

Texture2D<float> ao_depth : register(t2);
RWTexture2D<unorm float> t_rw_ssao : register(u0);

#define LINE_SWEEP_OWN_CAMERA
#include "../twwarhammer3/line_sweep_bitmask.hlsli"

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 coords : SV_DispatchThreadID, uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID,
          uint thread_index : SV_GroupIndex) {
  uint2 size;
  t_rw_ssao.GetDimensions(size.x, size.y);
#ifdef AO_UPSAMPLE
  BitmaskUpsample(coords.xy, size);
#else
  BitmaskCollect(coords.xy, group.xy, thread.xy, thread_index, size);
#endif
}
