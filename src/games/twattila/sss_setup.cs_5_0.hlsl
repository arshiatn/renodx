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

struct DispatchData
{
  int3 wave_count;
  int2 wave_offset;
};

[numthreads(1, 1, 1)]
void main()
{
  const int wave_size = WAVE_SIZE;
  const uint sun_index = (uint)g_sss_sun_direction;
  const float3 sun_direction = float3(LightingFloat(sun_index), LightingFloat(sun_index + 1u), LightingFloat(sun_index + 2u));
  const float4 light_projection = mul(float4(-sun_direction, 0.f), view_projection);
  // The full-size screen (bend_sss_gpu.hlsli maps its pixels into the rendered part below 100%: with the render size
  // here too only the top-left part of the picture got shadows).
  const int2 viewport_size = int2(g_sss_size);
  const int2 min_render_bounds = 0;
  const int2 max_render_bounds = viewport_size;

  // Floating point division in the shader has a practical limit for precision when the light is *very* far off screen
  // (~1m pixels+). So when computing the light XY coordinate, use an adjusted w value to handle these extreme values.
  float xy_light_w = light_projection.w;
  const float fp_limit = 0.000002f * (float)wave_size;
  if (xy_light_w >= 0 && xy_light_w < fp_limit) {
    xy_light_w = fp_limit;
  } else if (xy_light_w < 0 && xy_light_w > -fp_limit) {
    xy_light_w = -fp_limit;
  }

  // Need precise XY pixel coordinates of the light.
  float4 light_coordinate;
  light_coordinate.x = ((light_projection.x / xy_light_w) * +0.5f + 0.5f) * (float)viewport_size.x;
  light_coordinate.y = ((light_projection.y / xy_light_w) * -0.5f + 0.5f) * (float)viewport_size.y;
  light_coordinate.z = (projection._m23 == 0) ? 0 : (projection._m22 / projection._m23);
  // The march direction with the same sign as the position's divisor (Bend's w > 0 test disagreed with it at w 0).
  light_coordinate.w = (xy_light_w > 0) ? 1 : -1;

  const int2 light_xy = int2((int)(light_coordinate.x + 0.5f), (int)(light_coordinate.y + 0.5f));

  // Make the bounds inclusive, relative to the light.
  const int biased_bounds[4] = {
      min_render_bounds.x - light_xy.x,
      -(max_render_bounds.y - light_xy.y),
      max_render_bounds.x - light_xy.x,
      -(min_render_bounds.y - light_xy.y),
  };

  DispatchData dispatches[8];
  int dispatch_count = 0;
  [unroll]
  for (int clear = 0; clear < 8; ++clear) {
    dispatches[clear].wave_count = 0;
    dispatches[clear].wave_offset = 0;
  }

  // Process 4 quadrants around the light center. They each form a rectangle with one corner on the light XY
  // coordinate. If the rectangle isn't square, it will need breaking in two on the larger axis.
  // 0 = bottom left, 1 = bottom right, 2 = top left, 3 = top right.
  [loop]
  for (int q = 0; q < 4; q++) {
    // Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally.
    const bool vertical = (q == 0 || q == 3);

    // Bounds relative to the quadrant.
    const int bounds[4] = {
        max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / wave_size,
        max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / wave_size,
        max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + wave_size * (vertical ? 1 : 2) - 1)) / wave_size,
        max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + wave_size * (vertical ? 2 : 1) - 1)) / wave_size,
    };

    if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0) {
      const int bias_x = (q == 2 || q == 3) ? 1 : 0;
      const int bias_y = (q == 1 || q == 3) ? 1 : 0;

      DispatchData disp;
      disp.wave_count = int3(wave_size, bounds[2] - bounds[0], bounds[3] - bounds[1]);
      disp.wave_offset.x = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
      disp.wave_offset.y = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

      // We want the far corner of this quadrant relative to the light, as we need to know where the diagonal light
      // ray intersects with the edge of the bounds.
      int axis_delta = +biased_bounds[0] - biased_bounds[1];
      if (q == 1) axis_delta = +biased_bounds[2] + biased_bounds[1];
      if (q == 2) axis_delta = -biased_bounds[0] - biased_bounds[3];
      if (q == 3) axis_delta = -biased_bounds[2] + biased_bounds[3];

      axis_delta = (axis_delta + wave_size - 1) / wave_size;

      if (axis_delta > 0) {
        // Take copy of current volume.
        DispatchData disp2 = disp;

        if (q == 0) {
          // Split on Y, split becomes -1 larger on x.
          disp2.wave_count.z = min(disp.wave_count.z, axis_delta);
          disp.wave_count.z -= disp2.wave_count.z;
          disp2.wave_offset.y = disp.wave_offset.y + disp.wave_count.z;
          disp2.wave_offset.x--;
          disp2.wave_count.y++;
        }
        if (q == 1) {
          // Split on X, split becomes +1 larger on y.
          disp2.wave_count.y = min(disp.wave_count.y, axis_delta);
          disp.wave_count.y -= disp2.wave_count.y;
          disp2.wave_offset.x = disp.wave_offset.x + disp.wave_count.y;
          disp2.wave_count.z++;
        }
        if (q == 2) {
          // Split on X, split becomes -1 larger on y.
          disp2.wave_count.y = min(disp.wave_count.y, axis_delta);
          disp.wave_count.y -= disp2.wave_count.y;
          disp.wave_offset.x += disp2.wave_count.y;
          disp2.wave_count.z++;
          disp2.wave_offset.y--;
        }
        if (q == 3) {
          // Split on Y, split becomes +1 larger on x.
          disp2.wave_count.z = min(disp.wave_count.z, axis_delta);
          disp.wave_count.z -= disp2.wave_count.z;
          disp.wave_offset.y += disp2.wave_count.z;
          disp2.wave_count.y++;
        }

        // Remove if too small (Bend's removal by moving the list's last entry onto it: here kept out of the list).
        if (disp.wave_count.y > 0 && disp.wave_count.z > 0) dispatches[dispatch_count++] = disp;
        if (disp2.wave_count.y > 0 && disp2.wave_count.z > 0) dispatches[dispatch_count++] = disp2;
      } else {
        dispatches[dispatch_count++] = disp;
      }
    }
  }

  // Scale the shader values by the wave count, the shader expects this.
  t_rw_parameters[0] = asint(light_coordinate);
  [unroll]
  for (int i = 0; i < 8; i++) {
    t_rw_arguments.Store3(i * 12, (i < dispatch_count) ? uint3(dispatches[i].wave_count) : uint3(0, 0, 0));
    t_rw_parameters[1 + i] = int4(dispatches[i].wave_offset * wave_size, 0, 0);
  }
}
