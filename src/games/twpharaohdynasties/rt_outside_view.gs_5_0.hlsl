/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Pharaoh's ray tracing scene: casters the main pass culled but the shadow pass still draws (twattila/rt_core.hpp,
// SceneShader::cascade_keep with Host::cascade_world). The game's shadow vertex shader runs as the game binds it (its own
// view-projection at cb0[9-12]; Warhammer III's rt_outside_view puts the main view's at cb0[10-13] instead) and writes each
// corner's world position to an output of its own; this reads it there and tests the triangle against the main view (b0[10-13],
// rt_core's cascade camera). A triangle in the view belongs to the main pass, which captured it: it collapses onto its first
// corner (the count stays: a stream output's place in the shared buffer is known before the draw). Only a triangle wholly
// outside stays. Out: world positions, w 1 or (leaves, far cards) the mask's UV as two halves (rt_capture.hlsli).
// The input is a prefix of the vertex shader's output signature (fxc /dumpbin of the game's bytes), one variant each:
//   RT_WORLD: DC5FE525 (fx__rigidcommon vs40_shadow) and 67E28AF9 (rigidterrain vs41_shadow_new_battle, world before its
//     z + 0.005): TEXCOORD0 the world.
//   RT_UNIT: BC8DA38B, 731EF7DD, 039A9620 (fx__weighted vs40_shadow): TEXCOORD1 the UV, TEXCOORD2 the world.
//   RT_BRANCH: 5AFB107C (rigidbranch_v3 vs40_shadow): TEXCOORD1 the UV, TEXCOORD5's xyz the world.
//   RT_LEAF: A709F459 (rigidleaf_v3 vs40_shadow): TEXCOORD1 the UV of 6CA31B12's t0 test, TEXCOORD2's xyz the world.
//   RT_CARD: EB2A56A1 (rigidtreebillboard vs40_shadow, turned to the shadow camera): TEXCOORD0 the card's UV and view (z; -1
//     one face) of 31718466's t2 test, TEXCOORD1's xyz the world.
//   RT_WORLD_UV: Warhammer II's 7D979F60 (rigidcampaignvegetation vs40_shadow): TEXCOORD0's xyz the world, TEXCOORD1 the UV of
//     5906CF6E's t0 test.
//   RT_CARD_SPLIT: Warhammer II's 75914932 (rigidtreebillboard vs40_shadow): TEXCOORD0 the card's UV, its view's whole part (z)
//     and the blend to the next (w) of B7ED5B72's t0 test, TEXCOORD1's xyz the world, TEXCOORD2's x 1 - the card's fade.
// CMake's shader rule takes no gs_ target: after a change run from the repository's root, once a variant
// bin\fxc.exe src\games\twpharaohdynasties\rt_outside_view.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /D RT_WORLD /Vn __rt_outside_view_world /Fh src\games\twpharaohdynasties\rt_outside_view_world_gs.h
// (RT_UNIT __rt_outside_view_unit _unit_gs.h, RT_BRANCH __rt_outside_view_branch _branch_gs.h, RT_LEAF __rt_outside_view_leaf
// _leaf_gs.h, RT_CARD __rt_outside_view_card _card_gs.h); Troy's leaves: /D RT_LEAF /D RT_NO_FADE /Vn __rt_outside_view_leaf
// /Fh src\games\twtroy\rt_outside_view_leaf_gs.h; Warhammer II's campaign trees: /D RT_WORLD_UV /Vn __rt_outside_view_world_uv
// /Fh src\games\twwarhammer2\rt_outside_view_world_uv_gs.h, its far cards: /D RT_CARD_SPLIT /Vn __rt_outside_view_card_split
// /Fh src\games\twwarhammer2\rt_outside_view_card_split_gs.h.
#include "../twattila/rt_capture.hlsli"

cbuffer MainView : register(b0) {
  float4 main_view[14];  // [10-13]: the main view's view-projection, rows acting on column vectors
}

#if defined(RT_WORLD)
struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
};
#elif defined(RT_UNIT)
struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD1;
  float4 world : TEXCOORD2;
};
#elif defined(RT_BRANCH)
struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD1;
  float4 world : TEXCOORD5;
};
#elif defined(RT_LEAF)
struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD1;
  float burn : TEXCOORD3;
#ifndef RT_NO_FADE
  float fade : TEXCOORD4;  // Troy's leaf shadow VS A295269F writes none: RT_NO_FADE (twtroy/rt_outside_view_leaf_gs.h)
#endif
  float4 world : TEXCOORD2;
};
#elif defined(RT_CARD)
struct Corner {
  float4 position : SV_Position;
  float3 uv_view : TEXCOORD0;
  float fade : TEXCOORD3;
  float4 world : TEXCOORD1;
};
#elif defined(RT_WORLD_UV)
struct Corner {
  float4 position : SV_Position;
  float4 world : TEXCOORD0;
  float2 uv : TEXCOORD1;
};
#elif defined(RT_CARD_SPLIT)
struct Corner {
  float4 position : SV_Position;
  float4 uv_view : TEXCOORD0;
  float4 world : TEXCOORD1;
  float3 unfaded : TEXCOORD2;
};
#endif

struct Output {
  float4 position : SV_Position;
};

[maxvertexcount(3)]
void main(triangle Corner corners[3], inout TriangleStream<Output> stream) {
  float3 x, y, z, w;
  [unroll]
  for (uint corner = 0u; corner < 3u; ++corner) {
    const float4 world = float4(corners[corner].world.xyz, 1.f);
    x[corner] = dot(world, main_view[10]);
    y[corner] = dot(world, main_view[11]);
    z[corner] = dot(world, main_view[12]);
    w[corner] = dot(world, main_view[13]);
  }
  const bool outside = all(w <= 0.f) || all(x > w) || all(x < -w) || all(y > w) || all(y < -w) || all(z > w);
#if defined(RT_CARD_SPLIT)
  // B7ED5B72 discards all of a card faded below half (its alpha x the fade x at most 1 against 0.5): collapsed too.
  const bool shown = outside && (corners[0].unfaded.x <= 0.5f);
#else
  const bool shown = outside;
#endif
  [unroll]
  for (uint vertex = 0u; vertex < 3u; ++vertex) {
    const Corner kept = corners[shown ? vertex : 0u];
    Output output;
#if defined(RT_LEAF) || defined(RT_WORLD_UV)
    output.position = RtWorldUv(kept.world.xyz, kept.uv);
#elif defined(RT_CARD)
    // 31718466 blends the two of the card's four views around z (a quarter of the UV each); the nearest here, as the main
    // pass's capture (rt_capture_far_tree) takes it. One face (z -1): its quarter is in the UV already.
    const float view = (kept.uv_view.z < 0.f) ? 0.f : fmod(floor(kept.uv_view.z + 0.5f), 4.f);
    output.position = RtWorldUv(kept.world.xyz, float2(kept.uv_view.x + (0.25f * view), kept.uv_view.y));
#elif defined(RT_CARD_SPLIT)
    // B7ED5B72 blends the view z and the next (a quarter further, wrapping) by w: the nearest, as RT_CARD.
    const float view = fmod(kept.uv_view.z + floor(kept.uv_view.w + 0.5f), 4.f);
    output.position = RtWorldUv(kept.world.xyz, float2(kept.uv_view.x + (0.25f * view), kept.uv_view.y));
#else
    output.position = float4(kept.world.xyz, 1.f);
#endif
    stream.Append(output);
  }
}
