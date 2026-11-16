/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows on an Unreal game (ue_screen_space_shadows.hpp), the dispatch list: Bend Studio's BuildDispatchList
// (twattila/sss_dispatch.hlsli) for this frame's View (b1) and sun (the includer's UeTowardSun(): xyz the direction
// toward the sun, w 1, or 0 for no shadowed sun, which lists nothing), on the GPU as the game's constants are. Bend's
// depth is ue_sss_depth's 1 - device Z (far 1): the sun's depth is 1 - ViewToClip's depth at infinity. Also the settings
// sss_bend and sss_blur read (u2, copied into their b2): the add-on's (b3) with the render size, ViewSizeAndInvSize.
// The includer defines UE_VIEW_SHIFT (ue_view.hlsli) and UeTowardSun() first (one reading the View includes
// ue_view.hlsli with UE_VIEW_REGISTER b1 before it).

// Copyright 2023 Sony Interactive Entertainment.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "../twattila/sss.hlsli"
#include "../twattila/sss_dispatch.hlsli"

#ifndef UE_VIEW_REGISTER
#define UE_VIEW_REGISTER b1
#endif
#include "./ue_view.hlsli"

// sss.hlsli's sss_settings as the add-on uploads them (render size 0).
cbuffer ue_sss_base : register(b3) {
  uint4 base_settings[6];
};

RWByteAddressBuffer t_rw_arguments : register(u0);
RWStructuredBuffer<int4> t_rw_parameters : register(u1);
RWByteAddressBuffer t_rw_settings : register(u2);

[numthreads(1, 1, 1)]
void main() {
  const int2 viewport_size = int2(UE_VIEW(UE_VIEW_SIZE_AND_INV_SIZE).xy);
  [unroll]
  for (uint row = 0; row < 6u; ++row) {
    t_rw_settings.Store4(row * 16u, base_settings[row]);
  }
  // g_sss_render_size: c4.yz.
  t_rw_settings.Store2(4u * 16u + 4u, asuint(float2(viewport_size)));

  const float4 toward_sun = UeTowardSun();
  const float4 light_projection = toward_sun.x * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP) + toward_sun.y * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP + 1u)
                                  + toward_sun.z * UE_VIEW(UE_TRANSLATED_WORLD_TO_CLIP + 2u);
  const float4 depth_row = UE_VIEW(UE_VIEW_TO_CLIP + 2u);
  const float light_depth = 1.f - ((depth_row.w == 0.f) ? 0.f : (depth_row.z / depth_row.w));
  const float4 light_coordinate = BendLightCoordinate(light_projection, light_depth, viewport_size, WAVE_SIZE);

  DispatchData dispatches[8];
  int dispatch_count = 0;
  BuildDispatchList(light_coordinate, int2(0, 0), viewport_size, WAVE_SIZE, dispatches, dispatch_count);
  if (toward_sun.w == 0.f || any(viewport_size <= 0)) dispatch_count = 0;

  // Scale the shader values by the wave count, the shader expects this.
  t_rw_parameters[0] = asint(light_coordinate);
  [unroll]
  for (int i = 0; i < 8; i++) {
    t_rw_arguments.Store3(i * 12, (i < dispatch_count) ? uint3(dispatches[i].wave_count) : uint3(0, 0, 0));
    t_rw_parameters[1 + i] = int4(dispatches[i].wave_offset * WAVE_SIZE, 0, 0);
  }
}
