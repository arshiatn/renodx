/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The sprite soldiers that a geometry shader expands (rt_core.hpp SceneShader::capture_geometry_shader): the game's imposter
// vertex shader passes each soldier's instance through as a point (vs40_main_gs: Warhammer 381E079E / 883D12E7, Troy,
// Warhammer II and Pharaoh 370A7440) and its gs_main makes the card (Warhammer 12D3A4BC / D6954DFC, Troy DC05F551,
// Warhammer II 29E8BDF7, Pharaoh 27A16995: the same code, register for register). This is that gs_main with the card's right
// vector turned to the sun (rt_sun's texel 0 at t0, as twattila/rt_capture_imposter.vs_4_0.hlsl's: normalised across, so its
// shadow keeps its width) and each corner's world position and UV streamed out (rt_capture.hlsli RtWorldUv; the corners in
// the game's strip order). Its input is the vertex shader's whole output signature (TEXCOORD2.xy and TEXCOORD3 sharing a
// register as the game's). WARP against the game's bytes: test/tw_rays_captures.
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twattila\rt_capture_imposter_gs.gs_4_0.hlsl -T gs_4_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_capture_imposter_gs /Fh src\games\twattila\rt_capture_imposter_gs.h
#include "./rt_capture.hlsli"

Texture2D<float4> t_rt_sun : register(t0);

struct ImposterPoint {
  float4 v0 : POSITION0;
  float4 v1 : POSITION1;
  float4 v2 : POSITION2;
  float4 v3 : TEXCOORD0;
  float4 v4 : TEXCOORD1;
  float2 v5 : TEXCOORD2;
  float2 v6 : TEXCOORD3;
};

struct Corner {
  float4 world_uv : TEXCOORD0;
};

// A corner's place: standing at `base` (v3.w up), or (v1.w below 0) flat on the ground (v3.z back, v3.y across).
float3 CardCorner(ImposterPoint p, float3 right, float3 base) {
  float3 standing = base;
  standing.y += p.v3.w;
  const float3 flat_card = right * p.v3.y + float3(standing.x, standing.y - p.v3.z, standing.z);
  return (p.v1.w >= 0.f) ? standing : flat_card;
}

[maxvertexcount(4)]
void main(point ImposterPoint input[1], inout TriangleStream<Corner> corners) {
  const ImposterPoint p = input[0];
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  // gs_main: (-r1.z, 0, r1.x) of the way to the camera, its length that way's across.
  const float3 right = (across > 1e-3f) ? float3(-toward_sun.z, 0.f, toward_sun.x) / across : float3(1.f, 0.f, 0.f);
  const bool stands = p.v1.w >= 0.f;
  Corner corner;

  // The first corner and the UV's frame (gs_main's r1, r4, r5).
  float4 r1, r4, r5;
  r1.x = 1.f;
  r1.y = -p.v6.y;
  r1.z = -p.v0.w * p.v6.x + p.v1.x;
  r1.w = p.v1.w * p.v6.x;
  r4.x = -r1.w * 8.f + r1.z;
  r1.z = -p.v6.x * 0.99000001f + r4.x;
  r1.w = floor(-r1.z);
  const bool wraps = r1.z < 0.f;
  r1.w = r1.w + 1.f;
  r4.y = p.v1.y;
  r1.xy = r1.xy * r1.ww + r4.xy;
  r1.xy = wraps ? r1.xy : r4.xy;
  r1.z = r1.y + p.v3.z + p.v6.y;
  corner.world_uv = RtWorldUv(CardCorner(p, right, right * p.v2.z + p.v0.xyz), stands ? r1.xz : p.v1.xy);
  corners.Append(corner);
  r5.z = r1.z;

  r5.xy = -p.v6.xy * float2(0.980000019f, 0.99000001f) + r1.xy;
  r4.x = p.v6.x * 0.980000019f + p.v1.x;
  r4.yw = p.v1.yx;
  corner.world_uv = RtWorldUv(CardCorner(p, right, right * -p.v2.z + p.v0.xyz), stands ? r5.xz : r4.xy);
  corners.Append(corner);

  const float3 top = p.v0.xyz + float3(0.f, p.v2.x, 0.f);
  float2 uv;
  uv.x = stands ? r1.x : r4.w;
  r1.w = r5.y + p.v3.z + p.v6.y;
  r4.z = -p.v6.y * 0.99000001f + p.v1.y;
  uv.y = stands ? r1.w : r4.z;
  corner.world_uv = RtWorldUv(CardCorner(p, right, right * p.v2.z + top), uv);
  corners.Append(corner);

  r5.w = r1.w;
  corner.world_uv = RtWorldUv(CardCorner(p, right, right * -p.v2.z + top), stands ? r5.xw : r4.xz);
  corners.Append(corner);
}
