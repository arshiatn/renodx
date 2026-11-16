/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The helper upscaler's result written back into the game's exposed scene (upscaling.hpp: the tone mapper's Base, drawn
// over in full right before the tone mapper reads it), squared back where d1_upscale_inputs took the square root. The
// Upscaling Debug View instead (1): the motion the upscaler got (t1) as the hue, brighter the faster, over the scene
// dimmed (t2, the upscaler's colour input), as Attila's.
#include "../twattila/motion.hlsli"

Texture2D<float4> t_output : register(t0);
Texture2D<float2> t_motion : register(t1);
Texture2D<float4> t_input : register(t2);

cbuffer upscale_output : register(b0) {
  float g_square_root;
  float g_debug_view;
  float2 g_padding;
};

float4 main(float4 position : SV_Position) : SV_Target {
  const int3 pixel = int3(position.xy, 0);
  if (g_debug_view == 1.f) {
    float3 scene = t_input.Load(pixel).rgb;
    if (g_square_root != 0.f) scene *= scene;
    const float3 dim = 0.1f * dot(scene, float3(0.2126f, 0.7152f, 0.0722f));
    return float4(max(MotionColour(t_motion.Load(pixel)), dim), 1.f);
  }
  float3 color = max(t_output.Load(pixel).rgb, 0.f);
  if (g_square_root != 0.f) color *= color;
  // Not finite: black (a bit test: fxc folds isfinite away).
  return float4(any((asuint(color) & 0x7f800000u) == 0x7f800000u) ? 0.f : color, 1.f);
}
