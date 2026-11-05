/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing: casters from the game's shadow pass (twattila/rt_core.hpp, SceneShader::cascade_keep with a rows_shader). Their
// shadow VS bind other resources than the main pass's, so these read only the draw's vertex buffers; one signature for the trees' four
// (D75E6E2B, AF3F74B2, 8F975E16, 2327ED2B and the main pass's 474F97FB, 783383C6), by their bytes.
#define RT_TREE_INPUTS \
  float4 v0 : POSITION0, \
  float4 v1 : POSITION1, \
  float4 v2 : POSITION2, \
  float4 v3 : POSITION3, \
  float4 v4 : POSITION4, \
  float2 v5 : POSITION5, \
  float4 v6 : POSITION6, \
  float4 v7 : POSITION7, \
  float4 v8 : TEXCOORD0, \
  float4 v9 : TEXCOORD1, \
  float4 v10 : TEXCOORD2, \
  float4 v11 : TEXCOORD3, \
  float4 v12 : TEXCOORD4, \
  float4 v13 : TEXCOORD5, \
  float4 v14 : TEXCOORD6, \
  float4 v15 : TEXCOORD7, \
  uint v16 : SV_InstanceID0,

// The game's world position without wind or the branch simulation: a tree out of view at rest.
#define RT_TREE_REST_WORLD \
  (v6.x * float3(dot(float4(v0.xyz, 1.f), v8), dot(float4(v0.xyz, 1.f), v9), dot(float4(v0.xyz, 1.f), v10)) \
   + (1.f - v6.x) * float3(dot(float4(v1.xyz, 1.f), v11), dot(float4(v1.xyz, 1.f), v12), dot(float4(v1.xyz, 1.f), v13)))

// cb0[10-13]: the main view (rt_core swaps it in); all 0: a new mesh's first rows, which go whole.
cbuffer MainView : register(b0) {
  float4 cascade_view[14];
}

// An instance whose origin is in the view is the main pass's. Its rows left out (rt_place: a translation past MAX_TRANSLATION, same test as
// its own "gone" for an undrawn instance in the view).
void LeaveOutInView(inout float4 row0, inout float4 row1, inout float4 row2) {
  const float4 origin = float4(row0.w, row1.w, row2.w, 1.f);
  const float x = dot(origin, cascade_view[10]);
  const float y = dot(origin, cascade_view[11]);
  const float w = dot(origin, cascade_view[13]);
  if (any(cascade_view[13] != 0.f) && w + x >= 0.f && w - x >= 0.f && w + y >= 0.f && w - y >= 0.f) {
    row0 = float4(0.f, 0.f, 0.f, 1e30f);
    row1 = row0;
    row2 = row0;
  }
}
