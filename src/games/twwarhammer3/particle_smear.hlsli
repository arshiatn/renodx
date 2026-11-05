/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The game's own TAA reads both of these to know where particles are (its t1 and t6). Blended particles have no motion of their
// own, so an upscaler keeps their old places as trails; presets L and M take no reactive mask. Where the particles' transmittance
// (43606BB8: exp2(-1.442695 z)) left the range it had around where the pixel was last frame, motion from outside the frame drops
// that pixel's history. The moments are drawn at half size and copied by point (A759AD5D), so a texel is 2 render pixels and the
// camera's jitter flips whole texels at every object's edge, paused too: those stay inside the texels on both sides, a puff moving
// more than a texel a frame doesn't. Slower puffs keep their trails. Row off: both unbound, they read 0, motion is untouched.
Texture2D<float4> t_zeroth_moment : register(t3);
Texture2D<float4> t_zeroth_moment_last : register(t4);

float2 ParticleSmearMotion(int2 pixel, float2 motion, float2 render_size) {
  const int2 last_pixel = pixel + int2(round(motion));
  const float now = exp2(-1.44269502f * t_zeroth_moment.Load(int3(pixel, 0)).x);
  float lowest = exp2(-1.44269502f * t_zeroth_moment_last.Load(int3(last_pixel, 0)).x);
  if (abs(now - lowest) <= 0.02f) return motion;
  float highest = lowest;
  for (int y = -2; y <= 2; y += 2) {
    for (int x = -2; x <= 2; x += 2) {
      const float last = exp2(-1.44269502f * t_zeroth_moment_last.Load(int3(last_pixel + int2(x, y), 0)).x);
      lowest = min(lowest, last);
      highest = max(highest, last);
    }
  }
  return (now < lowest - 0.02f || now > highest + 0.02f) ? (render_size * 2.f) : motion;
}
