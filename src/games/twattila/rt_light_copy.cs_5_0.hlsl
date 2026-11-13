/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The reflection pass's light buffer as R16G16B16A16F (Warhammer III's march 3AC630DF reads it so, Pharaoh's backward_ssr
// 9A55B7F2 too) into the helper's light for the rays' hits (R11G11B10F), over the traced render size alone (rt_core.hpp
// Reflections' `light_buffer`, Host::light_copy).
Texture2D<float4> light : register(t0);
RWTexture2D<float3> copy : register(u0);
cbuffer constants : register(b0) {
  uint2 traced_size;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (any(id.xy >= traced_size)) return;
  copy[id.xy] = max(light[id.xy].rgb, 0.f);
}
