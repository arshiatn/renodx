/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a ship's rope as the shadow pass draws it (rope vs30_depth, 4E3EE717: a ribbon along the rope turned to
// the shadow camera, v3.x wide) for the outside-view geometry shader (twpharaohdynasties RT_WORLD: TEXCOORD0 the world): the
// game's corner in world space (its pixel shader 02314798 tests nothing: solid).
cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view_projection : packoffset(c10);
}

struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
};

Corner main(float4 v0 : POSITION0, float3 v1 : NORMAL0, float4 v3 : TEXCOORD1)
{
  const float3 across = normalize(cross(camera_position - v0.xyz, v1));
  Corner corner;
  corner.world = float4((v0.xyz + (across * v3.x)) / v0.w, 1.f);
  corner.position = mul(corner.world, view_projection);
  return corner;
}
