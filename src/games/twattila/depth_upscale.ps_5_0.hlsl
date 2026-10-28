/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// DLSS below 100%, after the tone mapper (anti_aliasing.hpp UpscaleDepth): the scene's depth (a copy, t0) holds the
// render size's picture in its top-left corner; stretched over the whole target (nearest) for the game's 3D sprites
// drawn at the display's size against it (the unit markers above the units).
Texture2D<float> t_depth : register(t0);

cbuffer depth_upscale : register(b0)
{
  float4 g_depth_share;  // xy: the render size over the display's
}

float main(float4 position : SV_Position) : SV_Depth
{
  return t_depth.Load(int3(int2(position.xy * g_depth_share.xy), 0));
}
