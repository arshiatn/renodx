/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a soldier's or horse's shadow drawn without the animation map (variantweighted vs30_shadow /
// vs30_shadow_blooded, 1E0425DE: two bones from variant_animation_VS's matrix stack at b1, then its instance's rows) for the
// outside-view geometry shader (twpharaohdynasties RT_WORLD: TEXCOORD0 the world): the game's corner in world space, as its
// vertex shader works it out from the same inputs (solid, as variantweightedvtf's in rt_capture_cascade_unit).
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
}

cbuffer variant_animation_VS : register(b1)
{
  float4 animation_matrix_stack[150] : packoffset(c0);
}

struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
};

// The bone at index * 255 (the vertex's byte) in the matrix stack: its three rows applied to `position`.
float3 Skinned(float bone_byte, float3 position)
{
  const int first = int(floor(255.009995f * bone_byte)) * 3;
  const float4 homogeneous = float4(position, 1.f);
  return float3(dot(animation_matrix_stack[first], homogeneous), dot(animation_matrix_stack[first + 1], homogeneous),
                dot(animation_matrix_stack[first + 2], homogeneous));
}

Corner main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v9 : TEXCOORD0,
  float4 v10 : TEXCOORD1,
  float4 v11 : TEXCOORD2)
{
  // The game's blend: the first bone (v3.w) by v5.w, the second (v4.w) by the rest, each with its own position.
  const float3 local = (v5.w * Skinned(v3.w, v0.xyz)) + ((1.f - v5.w) * Skinned(v4.w, v1.xyz));
  const float4 placed = float4(local, 1.f);
  Corner corner;
  corner.world = float4(dot(v9, placed), dot(v10, placed), dot(v11, placed), 1.f);
  corner.position = mul(corner.world, view_projection);
  return corner;
}
