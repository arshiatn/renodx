/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TALESOFARISE_DLSS_BEND_DISPATCH_SETUP_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_BEND_DISPATCH_SETUP_HLSLI_

// Screen-space shadows on a game the add-on drives through ue_screen_space_shadows.hpp, the dispatch list: Bend Studio's
// BuildDispatchList (twattila/sss_dispatch.hlsli) on the GPU, as the game's constants are, for what the includer reads
// from them (WriteBendDispatchList: the render size, the sun's light projection and its depth in Bend's depth, which
// is the add-on's depth pass's). Also the settings sss_bend and sss_blur read (u2, copied into their b2): the add-on's
// (b3) with the render size. Each game's setup shader (Unreal's: ue_sss_setup.hlsli) includes it and calls it from its
// main.

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

// sss.hlsli's sss_settings as the add-on uploads them (render size 0).
cbuffer ue_sss_base : register(b3) {
  uint4 base_settings[6];
};

RWByteAddressBuffer t_rw_arguments : register(u0);
RWStructuredBuffer<int4> t_rw_parameters : register(u1);
RWByteAddressBuffer t_rw_settings : register(u2);

// The list for `viewport_size` (the render size) and the sun: `light_projection` the direction toward it through the
// camera's view-projection (w 0 in, clip space out), `light_depth` its depth (Bend's: the depth pass's at infinity);
// `listed` false (no shadowed sun) lists nothing.
void WriteBendDispatchList(int2 viewport_size, float4 light_projection, float light_depth, bool listed) {
  [unroll]
  for (uint row = 0; row < 6u; ++row) {
    t_rw_settings.Store4(row * 16u, base_settings[row]);
  }
  // g_sss_render_size: c4.yz.
  t_rw_settings.Store2(4u * 16u + 4u, asuint(float2(viewport_size)));

  const float4 light_coordinate = BendLightCoordinate(light_projection, light_depth, viewport_size, WAVE_SIZE);
  DispatchData dispatches[8];
  int dispatch_count = 0;
  BuildDispatchList(light_coordinate, int2(0, 0), viewport_size, WAVE_SIZE, dispatches, dispatch_count);
  if (!listed || any(viewport_size <= 0)) dispatch_count = 0;

  // Scale the shader values by the wave count, the shader expects this.
  t_rw_parameters[0] = asint(light_coordinate);
  [unroll]
  for (int i = 0; i < 8; i++) {
    t_rw_arguments.Store3(i * 12, (i < dispatch_count) ? uint3(dispatches[i].wave_count) : uint3(0, 0, 0));
    t_rw_parameters[1 + i] = int4(dispatches[i].wave_offset * WAVE_SIZE, 0, 0);
  }
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_BEND_DISPATCH_SETUP_HLSLI_
