/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Contact shadows (contact_shadows.hpp): which shadow mask slice and channels hold the sun's shadow, from the tiled
// lighting's own light list (cs 0x897232B5's t32; the sun is lightType 8): u0 = slice, 4 channel weights as bits.
struct TiledLightShadeInfo {
  uint light_type;
  uint resource_index;
  uint shadow_mask_index;
  uint padding;
  float4 position_radius;
  float2 attenuation;
  float2 shadow_parameters;
  float3x4 clip_volume;
  float4 color;
  float4x4 projector;
  float4x4 shadow;
  float4 shadow_channel;
};

StructuredBuffer<TiledLightShadeInfo> lights : register(t0);
RWBuffer<uint> sun : register(u0);

[numthreads(1, 1, 1)]
void main() {
  uint count;
  uint stride;
  lights.GetDimensions(count, stride);
  uint2 found = uint2(0xFFFFFFFFu, 0u);
  [loop]
  for (uint index = 0; index < min(count, 1024u); ++index) {
    if (lights[index].light_type == 8u) {
      const float4 channel = lights[index].shadow_channel;
      found = uint2(lights[index].shadow_mask_index,
                    (channel.x > 0.5f ? 1u : 0u) | (channel.y > 0.5f ? 2u : 0u) | (channel.z > 0.5f ? 4u : 0u) | (channel.w > 0.5f ? 8u : 0u));
      break;
    }
  }
  sun[0] = found.x;
  sun[1] = found.y;
}
