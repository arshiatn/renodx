/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's reflections (addon.cpp reflections::OnMarch): the frame's light buffer as the game's own march reads it
// (R16G16B16A16F, what 3AC630DF copies into the radiance's first level) into the format the helper's light takes (R11G11B10F,
// twattila/rt_core.hpp Reflections' `light_buffer`).
Texture2D<float4> light : register(t0);
RWTexture2D<float3> copy : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  copy[id.xy] = max(light[id.xy].rgb, 0.f);
}
