/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Bend's screen-space shadows (screen_space.hpp, named as talesofarise_dlss/ue_screen_space_shadows.hpp embeds it), the
// dispatch list: Bend Studio's BuildDispatchList (twattila/sss_dispatch.hlsli) for Divinity 2's main camera and sun as
// the sun mask's draw reads them, on the GPU as the game's constants are: PerFrame (b0; its LightPropertyMatrix
// column-major, row 0 = c0-c2's x, toward the sun) and PerView (b1; GlobalConstants_DX11.shdh: ViewProjection rows acting
// on column vectors, ViewInfo's size the render size). Bend's depth is the standard device depth (ue_sss_depth: far 1):
// the sun's is the projection's depth at infinity. Also the settings sss_bend and sss_blur read (u2, copied into their
// b2): the add-on's (b3) with the render size. As talesofarise_dlss/ue_sss_setup.hlsli, on this game's constants.

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

cbuffer PerFrame : register(b0) {
  float4 frame_registers[3];
};

cbuffer PerView : register(b1) {
  row_major float4x4 d2_view;
  row_major float4x4 d2_projection;
  row_major float4x4 d2_view_projection;
  float4 d2_view_position;
  float4 d2_view_info;  // x the far plane, y the near plane, zw the render size
  float4 d2_scale_and_bias;
};

// sss.hlsli's sss_settings as the add-on uploads them (render size 0).
cbuffer ue_sss_base : register(b3) {
  uint4 base_settings[6];
};

RWByteAddressBuffer t_rw_arguments : register(u0);
RWStructuredBuffer<int4> t_rw_parameters : register(u1);
RWByteAddressBuffer t_rw_settings : register(u2);

[numthreads(1, 1, 1)]
void main() {
  const int2 viewport_size = int2(d2_view_info.zw);
  [unroll]
  for (uint row = 0; row < 6u; ++row) {
    t_rw_settings.Store4(row * 16u, base_settings[row]);
  }
  // g_sss_render_size: c4.yz.
  t_rw_settings.Store2(4u * 16u + 4u, asuint(float2(viewport_size)));

  const float3 toward_sun = float3(frame_registers[0].x, frame_registers[1].x, frame_registers[2].x);
  const float4 light_projection = mul(d2_view_projection, float4(toward_sun, 0.f));
  const float light_depth = (d2_projection[3].z == 0.f) ? 0.f : (d2_projection[2].z / d2_projection[3].z);
  const float4 light_coordinate = BendLightCoordinate(light_projection, light_depth, viewport_size, WAVE_SIZE);

  DispatchData dispatches[8];
  int dispatch_count = 0;
  BuildDispatchList(light_coordinate, int2(0, 0), viewport_size, WAVE_SIZE, dispatches, dispatch_count);
  if (!(dot(toward_sun, toward_sun) > 0.f) || any(viewport_size <= 0)) dispatch_count = 0;

  // Scale the shader values by the wave count, the shader expects this.
  t_rw_parameters[0] = asint(light_coordinate);
  [unroll]
  for (int i = 0; i < 8; i++) {
    t_rw_arguments.Store3(i * 12, (i < dispatch_count) ? uint3(dispatches[i].wave_count) : uint3(0, 0, 0));
    t_rw_parameters[1 + i] = int4(dispatches[i].wave_offset * WAVE_SIZE, 0, 0);
  }
}
