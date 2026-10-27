/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#ifndef SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GEOMETRY_HLSLI_
#define SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GEOMETRY_HLSLI_

// Light Bounce (Experimental): the prepared geometry (light_bounce_prepare, its levels light_bounce_levels) for the
// trace, the resolve and the denoiser: each texel's depth buffer value and normal, its position rebuilt from its pixel.
// The includer's light_bounce.hlsli defines BounceViewPositionAt first.
#include "./light_bounce.h"

Texture2D<uint2> t_prepared_geometry : register(t47);  // depth buffer value, normal (octahedron, 16 bits per axis)

struct BounceGeometry {
  float3 position;
  float3 normal;
};

// At `level` of the prepared textures (`size`: the first level's).
BounceGeometry LoadGeometry(int2 texel, uint level, uint2 size) {
  const uint2 packed = t_prepared_geometry.Load(int3(texel, level));
  // The pixel the texel holds: each level took one of the 2 x 2 above it (light_bounce_levels, clamped at the edge), the
  // first level each texel's top-left pixel.
  uint2 pixel = uint2(texel);
  for (uint above = level; above > 0u; --above) {
    pixel = min(pixel * 2u + uint2(pixel.y & 1u, pixel.x & 1u), BounceRegionLast(above - 1u, max(size >> (above - 1u), 1u)));
  }
  const float2 octahedron = float2(packed.y & 0xFFFFu, packed.y >> 16) / 65535.f * 2.f - 1.f;
  float3 normal = float3(octahedron, 1.f - abs(octahedron.x) - abs(octahedron.y));
  normal.xy += (normal.xy >= 0.f) ? -saturate(-normal.z) : saturate(-normal.z);
  BounceGeometry geometry;
  geometry.position = BounceViewPositionAt(int2(pixel * (uint)LB.scale), asfloat(packed.x));
  geometry.normal = normalize(normal);
  return geometry;
}

#endif  // SRC_GAMES_TWATTILA_LIGHT_BOUNCE_GEOMETRY_HLSLI_
