/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Ray-traced sun shadows in place of the game's shadow filter (ray_tracing.hpp: CascadedPCSS 0x8F114E47, CascadedUnfiltered
// 0x7697084B, CascadedVSM 0x1C0323E3 left out): the traced visibility (t0, R8, 1 lit, the render pixels) into the same
// mask, every channel as those write it (Out.color = shadowValue), so the sun pass (0xCCCB7253 t6) and the forward
// materials that read the mask read the rays alone.
Texture2D<float> t_visibility : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
  return t_visibility.Load(int3(position.xy, 0));
}
