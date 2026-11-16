/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Screen-space shadows (ue_effects.hpp): Bend's occlusion into Unreal's light attenuation before the sun's deferred light
// draw. Its blue (light function x per-object shadowing, stored as a square root, multiplied into the light's surface
// and subsurface shadow) x sqrt(1 - the occlusion), a multiply blend on blue only: the sun's light x (1 - the occlusion).
Texture2D<float> occlusion : register(t0);  // 1 - Bend's shadow term (the scene depth's size)

float4 main(float4 position : SV_Position) : SV_Target {
  return float4(1.f, 1.f, sqrt(saturate(1.f - occlusion.Load(int3(position.xy, 0)))), 1.f);
}
