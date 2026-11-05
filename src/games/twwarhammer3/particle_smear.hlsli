/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The game's own TAA reads both of these to know where particles are (its t1 and t6). Blended particles have no motion of their
// own, so an upscaler keeps their old places as trails; presets L and M take no reactive mask. Where the particles' transmittance
// (43606BB8: exp2(-1.442695 z)) changed against where the pixel was last frame, motion from outside the frame drops that pixel's
// history. Row off: both unbound, they read 0, motion is untouched.
Texture2D<float4> t_zeroth_moment : register(t3);
Texture2D<float4> t_zeroth_moment_last : register(t4);

float2 ParticleSmearMotion(int2 pixel, float2 motion, float2 render_size) {
  const int2 last_pixel = pixel + int2(round(motion));
  const float now = exp2(-1.44269502f * t_zeroth_moment.Load(int3(pixel, 0)).x);
  const float last = exp2(-1.44269502f * t_zeroth_moment_last.Load(int3(last_pixel, 0)).x);
  return (abs(now - last) > 0.02f) ? (render_size * 2.f) : motion;
}
