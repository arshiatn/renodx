/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Shogun 2 ray tracing: what a cut-out shadow caster's capture version (cloth, sprite soldiers) writes for the shared outside-view
// geometry shader (twpharaohdynasties/rt_outside_view.gs_5_0.hlsl, RT_LEAF: __rt_outside_view_leaf). Its Corner's signature in
// its order, so both pack alike (TEXCOORD1, 3 and 4 share a register).
struct RtCascadeLeaf {
  float4 position : SV_Position;
  float2 uv : TEXCOORD1;
  float burn : TEXCOORD3;
  float fade : TEXCOORD4;
  float4 world : TEXCOORD2;
};
