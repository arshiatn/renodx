/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows on an Unreal game (ue_screen_space_shadows.hpp), the dispatch list (bend_dispatch_setup.hlsli)
// for this frame's View (b1) and sun (the includer's UeTowardSun(): xyz the direction toward the sun, w 1, or 0 for no
// shadowed sun, which lists nothing). Bend's depth is ue_sss_depth's 1 - device Z (far 1): the sun's depth is 1 -
// ViewToClip's depth at infinity. The render size: ViewSizeAndInvSize.
// The includer defines UE_VIEW_SHIFT (ue_view.hlsli) and UeTowardSun() first (one reading the View includes
// ue_view.hlsli with UE_VIEW_REGISTER b1 before it).

#include "./bend_dispatch_setup.hlsli"

#ifndef UE_VIEW_REGISTER
#define UE_VIEW_REGISTER b1
#endif
#include "./ue_view.hlsli"

[numthreads(1, 1, 1)]
void main() {
  const float4 toward_sun = UeTowardSun();
  const float4 light_projection = toward_sun.x * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP) + toward_sun.y * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP + 1u)
                                  + toward_sun.z * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP + 2u);
  const float4 depth_row = UE_VIEW(UE_VIEW_TO_CLIP + 2u);
  const float light_depth = 1.f - ((depth_row.w == 0.f) ? 0.f : (depth_row.z / depth_row.w));
  WriteBendDispatchList(int2(UE_VIEW(UE_VIEW_SIZE_AND_INV_SIZE).xy), light_projection, light_depth, toward_sun.w != 0.f);
}
