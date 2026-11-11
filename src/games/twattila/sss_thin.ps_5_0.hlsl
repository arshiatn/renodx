/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Through Banners (screen_space_shadows.hpp InstallThin): a banner's, flag's, tent's or sail's G-buffer draw drawn
// again where it is the scene's surface (its depth, the game's vertex shader's: their outputs start with SV_Position) into
// the banners' depth, which the extended march compares with the scene's.
float main(float4 position : SV_Position) : SV_Target
{
  return position.z;
}
