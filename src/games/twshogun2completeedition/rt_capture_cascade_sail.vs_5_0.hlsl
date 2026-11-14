/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: a ship's sail as the shadow pass draws it (sail / sailsymbol vs30_depth, A42F9EDE / F7EDF855: the mesh
// placed by its draw's world_matrix) for the outside-view geometry shader: its corner in world space and the UV of its pixel
// shader's first mask test (05070100: the diffuse map's alpha at t0; the damage holes of its second, s_alpha_map, left out).
#include "./rt_cascade_leaf.hlsli"

cbuffer sail_VS : register(b1)
{
  float4x3 world_matrix : packoffset(c0);
}

RtCascadeLeaf main(float4 v0 : POSITION0, float4 v2 : TEXCOORD1)
{
  RtCascadeLeaf leaf;
  leaf.position = 0.f;
  leaf.uv = v2.xy;
  leaf.burn = 0.f;
  leaf.fade = 1.f;
  leaf.world = float4(mul(v0, world_matrix), 1.f);
  return leaf;
}
