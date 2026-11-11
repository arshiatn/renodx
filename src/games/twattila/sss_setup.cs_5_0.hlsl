// Screen-space shadows (screen_space_shadows.hpp), the dispatch list: Bend Studio's BuildDispatchList (bend_sss_cpu.h)
// run on the GPU for this frame's camera and sun (the game's constants are on the GPU only): the sun's direction
// projected to the screen, then the up to 8 dispatches around it as indirect arguments (u0: 8 x 3 group counts,
// unused ones 0) and their parameters (u1: [0] the light coordinate, [1 + i] dispatch i's wave offset).
// Changed from Bend's: written in HLSL; the light projection from the game's view_projection and sun_direction (the
// light's travel direction: towards the light is its negative; where it sits in lighting_VS_PS differs by game:
// g_sss_sun_direction), the viewport the depth's size (b2); the light's depth the projection's depth at infinity (Bend:
// 0 when the light is side-on, w 0, which shadowed flat ground everywhere). The camera is in Attila's layout.

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

// If you have feedback, or found this code useful, we'd love to hear from you.
// https://www.bendstudio.com
// https://www.twitter.com/bendstudio
//
// We are *always* looking for talented graphics and technical programmers!
// https://www.bendstudio.com/careers

#include "./sss.hlsli"
#include "./sss_dispatch.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
}

cbuffer lighting_VS_PS : register(b1)
{
  float4 lighting_registers[4];
}

float LightingFloat(uint index)
{
  return lighting_registers[index / 4u][index % 4u];
}

RWByteAddressBuffer t_rw_arguments : register(u0);
RWStructuredBuffer<int4> t_rw_parameters : register(u1);

[numthreads(1, 1, 1)]
void main()
{
  const int wave_size = WAVE_SIZE;
  const uint sun_index = (uint)g_sss_sun_direction;
  const float3 sun_direction = float3(LightingFloat(sun_index), LightingFloat(sun_index + 1u), LightingFloat(sun_index + 2u));
  const float4 light_projection = mul(float4(-sun_direction, 0.f), view_projection);
  // The rendered part's pixels (bend_sss_gpu.hlsli marches them).
  const int2 viewport_size = int2(g_sss_render_size);
  const int2 min_render_bounds = 0;
  const int2 max_render_bounds = viewport_size;

  // The light's depth: the projection's at infinity (Bend: 0 when the light is side-on, w 0, which shadowed flat ground
  // everywhere).
  const float4 light_coordinate = BendLightCoordinate(light_projection, (projection._m23 == 0) ? 0 : (projection._m22 / projection._m23),
                                                      viewport_size, wave_size);

  DispatchData dispatches[8];
  int dispatch_count = 0;
  BuildDispatchList(light_coordinate, min_render_bounds, max_render_bounds, wave_size, dispatches, dispatch_count);

  // Scale the shader values by the wave count, the shader expects this.
  t_rw_parameters[0] = asint(light_coordinate);
  [unroll]
  for (int i = 0; i < 8; i++) {
    t_rw_arguments.Store3(i * 12, (i < dispatch_count) ? uint3(dispatches[i].wave_count) : uint3(0, 0, 0));
    t_rw_parameters[1 + i] = int4(dispatches[i].wave_offset * wave_size, 0, 0);
  }
}
