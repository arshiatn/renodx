/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray tracing's scene data passes (dlaa_helper/rt_d3d12.hpp): rt_scene_data.cs_6_5.hlsl for the captures,
// rt_compact.cs_6_5.hlsl for the compact draws (bridge v16). Per triangle its normal into the normal pool and a leaf's
// three UVs (the vertex texture's w: two halves) into the UV pool. Garbage triangles go empty (NaN: left out of the
// BLASes) and are counted with the first capture or draw that had one: a coordinate past MAX_COORDINATE, or an edge past
// a part's size (MAX_PART: a unit's, grass's or cloth's) or MAX_EDGE. A vertex millions of metres out spread the scene's
// bounds until every ray met every instance (traces of a second, GPU resets, ATN 2026-10-28).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_DATA_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_DATA_HLSLI_

#include "./rt_scene.hlsli"

// The request's vertices, 16 bytes each (w: a leaf vertex's UV as two halves).
RWByteAddressBuffer vertices_written : register(u6);
// Triangles left out: far off, stretched parts, stretched others; an example's source (the first thread to claim it) and
// its three vertices (rt_d3d12.hpp LEFT_OUT_BYTES).
RWByteAddressBuffer left_out : register(u7);
RWBuffer<uint4> records_written : register(u3);
RWBuffer<uint> uv_pool_written : register(u4);
RWBuffer<uint> normal_pool_written : register(u5);

static const float LEAF_ALPHA_CUTOFF = 0.5f;  // the leaves' prepass shader: diffuse alpha from 0.5
static const uint CAPTURE_TERRAIN = 1u;  // dlaa_bridge.h RT_TERRAIN
static const uint CAPTURE_DYNAMIC = 2u;  // RT_DYNAMIC
static const float MAX_COORDINATE = 2.0e5f;  // metres (battle maps: a few kilometres)
static const float MAX_PART = 200.f;         // a unit's, grass's or cloth's triangle edge at most
static const float MAX_EDGE = 5000.f;
static const uint NO_UVS = 0xFFFFFFFFu;

// The triangle of `vertex` (a part's: MAX_PART), garbage counted with `source`; its normal at `normal`, a leaf's UVs from
// `uv` (NO_UVS: none).
void WriteTriangle(uint3 vertex, bool part, uint source, uint normal, uint uv) {
  const float4 a = asfloat(vertices_written.Load4(vertex.x * 16u));
  const float4 b = asfloat(vertices_written.Load4(vertex.y * 16u));
  const float4 c = asfloat(vertices_written.Load4(vertex.z * 16u));
  const bool far_off = any(abs(a.xyz) > MAX_COORDINATE) || any(abs(b.xyz) > MAX_COORDINATE) || any(abs(c.xyz) > MAX_COORDINATE);
  const float longest = max(max(distance(a.xyz, b.xyz), distance(b.xyz, c.xyz)), distance(c.xyz, a.xyz));
  if (far_off || longest > (part ? MAX_PART : MAX_EDGE)) {
    const uint empty = 0x7FC00000u;
    vertices_written.Store4(vertex.x * 16u, uint4(empty, empty, empty, asuint(a.w)));
    vertices_written.Store4(vertex.y * 16u, uint4(empty, empty, empty, asuint(b.w)));
    vertices_written.Store4(vertex.z * 16u, uint4(empty, empty, empty, asuint(c.w)));
    left_out.InterlockedAdd(far_off ? 0u : (part ? 4u : 8u), 1u);
    uint unclaimed = 0u;
    left_out.InterlockedCompareExchange(12u, 0xFFFFFFFFu, source, unclaimed);
    if (unclaimed == 0xFFFFFFFFu) {
      left_out.Store4(16u, asuint(a));
      left_out.Store4(32u, asuint(b));
      left_out.Store4(48u, asuint(c));
    }
  }
  const float3 face = cross(b.xyz - a.xyz, c.xyz - a.xyz);
  const float face_length = length(face);
  normal_pool_written[normal] = (face_length > 0.f) ? OctahedralEncode(face / face_length) : OctahedralEncode(float3(0.f, 1.f, 0.f));
  if (uv != NO_UVS) {
    uv_pool_written[uv] = asuint(a.w);
    uv_pool_written[uv + 1u] = asuint(b.w);
    uv_pool_written[uv + 2u] = asuint(c.w);
  }
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_SCENE_DATA_HLSLI_
