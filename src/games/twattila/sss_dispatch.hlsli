/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Bend Studio's BuildDispatchList (bend_sss_cpu.h) in HLSL, for sss_setup (the sun) and sss_lights (torches and fires):
// the up to 8 dispatches of waves around a light's screen point that cover a rectangle of render pixels.

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

#ifndef SRC_GAMES_TWATTILA_SSS_DISPATCH_HLSLI_
#define SRC_GAMES_TWATTILA_SSS_DISPATCH_HLSLI_

struct DispatchData
{
  int3 wave_count;
  int2 wave_offset;
};

// Bend's light coordinate: the light's pixel position in a viewport of `viewport_size` for its projection
// (`light_projection`: a point light's position or a direction's, x the view projection), its depth `light_z` (the
// march's projective depth), the march direction with the same sign as the position's divisor in w (Bend's w > 0 test
// disagreed with it at w 0).
float4 BendLightCoordinate(float4 light_projection, float light_z, int2 viewport_size, int wave_size)
{
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
  light_coordinate.z = light_z;
  light_coordinate.w = (xy_light_w > 0) ? 1 : -1;
  return light_coordinate;
}

// The light's pixel position (`light_coordinate`, Bend's: xy pixels) and the rectangle's bounds (pixels, max exclusive).
// Unused dispatches 0.
void BuildDispatchList(float4 light_coordinate, int2 min_render_bounds, int2 max_render_bounds, int wave_size,
                       out DispatchData dispatches[8], out int dispatch_count)
{
  const int2 light_xy = int2((int)(light_coordinate.x + 0.5f), (int)(light_coordinate.y + 0.5f));

  // Make the bounds inclusive, relative to the light.
  const int biased_bounds[4] = {
      min_render_bounds.x - light_xy.x,
      -(max_render_bounds.y - light_xy.y),
      max_render_bounds.x - light_xy.x,
      -(min_render_bounds.y - light_xy.y),
  };

  dispatch_count = 0;
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
}

#endif  // SRC_GAMES_TWATTILA_SSS_DISPATCH_HLSLI_
