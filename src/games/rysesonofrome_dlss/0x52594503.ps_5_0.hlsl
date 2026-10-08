/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private copy of t1 at the same pixel; values pass through. While DLSS runs: the stand-in for the game's SMAA
// neighborhood blending (0xF2D692D1, t1 its color input, so DLSS gets the frame without SMAA's edge blur), and for the
// native upscale (0xCD8548F6) of a frame whose post pass was drawn at the display size (t1 that result).
Texture2D<float4> SceneColor : register(t1);

// Only SV_Position: it links with any of the game's fullscreen vertex shaders.
float4 main(float4 v0 : SV_Position0) : SV_Target0 {
  return SceneColor.Load(int3(v0.xy, 0));
}
