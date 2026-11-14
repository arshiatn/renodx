/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rome II's smoke shadows (twattila/rt_core.hpp CaptureParticles, a SmokeVertexShader's quad shader): each smoke effect
// has its own generated vertex and pixel shader, and draws each particle as an instance of a quad (6 indices, vb0 the
// corner, vb1 the particle), so its stream output gives corners, not particles. This takes each instance's first triangle
// (SV_PrimitiveID even: two a quad) and writes one record (rt_smoke's three float4): the centre and the size of the quad
// from its three corners, colour, and the opacity the effect's pixel shader draws at that centre. One record an instance,
// also for particles the vertex shader culled (all zero: NaN centre, opacity 0): a record's place in the stream is its
// instance's.
// - The quad's own coordinates: the uv (TEXCOORD0), the vertex shader's cell plus the corner over its cells: affine in the
//   corner. The centre is the middle of the uv's span (a triangle of the quad holds both ends of each axis), placed from
//   the triangle's barycentric coordinates; the size from the corners' world spans (TEXCOORD1) along the uv's two axes.
// - Opacity as the effect's pixel shader computes its alpha (one build of this a pixel shader, PS_<hash> below): an emitter
//   type (TEXCOORD4.x) it lights takes its diffuse (TEXCOORD4.y: t0-t6, s2-s8) at the uv, blended to the next cell's
//   (TEXCOORD7: xy, z the blend, x 0 none) at the mip level its sample takes at that centre, times COLOR's w; a type it
//   does not light casts nothing (fire's glow), its soft edge (the scene's depth) depends on the view: left out.
//   The pixels are g_viewport_dimensions' (camera c29): the effects' own viewport, at any of their sizes (all 3,025 smoke
//   draws of ATN's runs: vp 3840x2160 to 1280x720, c29 the same). A particle around the camera casts none.
// The input is the vertex shaders' whole output signature (the three alike: fxc /dumpbin of the game's bytes).
// CMake's shader rule takes no gs_ target: after a change run from the repository's root, for each of 18FE5E2B, 085DDEDD,
// B5D63E8A (here 18FE5E2B)
// bin\fxc.exe src\games\twrome2\rt_particle_quads.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /D PS_18FE5E2B /Vn __rt_particle_quads_18FE5E2B /Fh src\games\twrome2\rt_particle_quads_18FE5E2B_gs.h

#include "../twattila/rt_particle_quads.hlsli"

#if defined(PS_18FE5E2B)
// impact_hit_generic_heavy (smokefromarrow00; vertex shader 7DF7491E): types 0-4 lit; diffuse 0-5 their own, any other
// the seventh; type 3 crushed by its age.
#define DIFFUSE_COUNT 7u
#define REST_LAST     true
#define LIT_TYPES     0x1Fu
#define CRUSH_TYPES   0x08u
#define WEIGHED_TYPES 0x00u
#elif defined(PS_085DDEDD)
// fire_smoke_loop, campaign_fire_smoke_loop (AF6C187A): types 0, 1, 2 and 5 lit; diffuse 0-5, any other none; types 0 and
// 2 times TEXCOORD2's w.
#define DIFFUSE_COUNT 6u
#define REST_LAST     false
#define LIT_TYPES     0x27u
#define CRUSH_TYPES   0x00u
#define WEIGHED_TYPES 0x05u
#elif defined(PS_B5D63E8A)
// destruction_spawnmesh_heavy (explosionsmoke00; B6F63295): types 0, 3, 4 and 5 lit; diffuse 0-5, any other none.
#define DIFFUSE_COUNT 6u
#define REST_LAST     false
#define LIT_TYPES     0x39u
#define CRUSH_TYPES   0x00u
#define WEIGHED_TYPES 0x00u
#endif

struct Corner {
  float4 position : SV_POSITION;  // clip space
  float4 colour : COLOR;          // w: the particle's alpha
  float2 uv : TEXCOORD0;          // the diffuse's
  float2 age : TEXCOORD5;         // x: over its life (0 to 1)
  float4 world : TEXCOORD1;       // the corner's world position
  float4 size : TEXCOORD2;        // w: the weight of types WEIGHED_TYPES
  float4 rotation : TEXCOORD3;
  float3 kind : TEXCOORD4;        // x: the emitter's type, y: its diffuse
  float3 seed : TEXCOORD6;
  float3 next : TEXCOORD7;        // xy: the next cell's uv (x 0: none), z: the blend to it
};

cbuffer camera : register(b0) {
  // The vertex shader's camera_VS_PS: [0] the camera's position, [29] g_viewport_dimensions.
  float4 camera[30];
}

Texture2D<float4> diffuse[DIFFUSE_COUNT] : register(t0);
SamplerState diffuse_samplers[DIFFUSE_COUNT] : register(s2);

// The pixel shader's diffuse `index` (past its last: the last with REST_LAST, else none) alpha at `uv`, at the level of a
// sample whose uv moves `uv_x`, `uv_y` along the quad's axes.
float DiffuseAlpha(uint index, float2 uv, float2 uv_x, float2 uv_y, float2x2 per_pixel) {
  const uint slot = REST_LAST ? min(index, DIFFUSE_COUNT - 1u) : index;
  float alpha = 0.f;
  [unroll]
  for (uint view = 0u; view < DIFFUSE_COUNT; ++view) {
    if (view == slot) {
      uint width = 0u;
      uint height = 0u;
      diffuse[view].GetDimensions(width, height);
      alpha = diffuse[view].SampleLevel(diffuse_samplers[view], uv, MipLevel(uv_x, uv_y, per_pixel, float2(width, height))).w;
    }
  }
  return alpha;
}

[maxvertexcount(1)]
void main(triangle Corner corners[3], uint primitive : SV_PrimitiveID, inout PointStream<Record> records) {
  if ((primitive & 1u) != 0u) return;
  const float2 along_first = corners[1].uv - corners[0].uv;
  const float2 along_second = corners[2].uv - corners[0].uv;
  const float area = Cross(along_first, along_second);
  float4 axis_x;
  float4 axis_y;
  Axes(float4(corners[0].world.xyz, 0.f), float4(corners[1].world.xyz, 0.f), float4(corners[2].world.xyz, 0.f), along_first, along_second, area,
       axis_x, axis_y);
  const float2 uv_min = min(min(corners[0].uv, corners[1].uv), corners[2].uv);
  const float2 uv_max = max(max(corners[0].uv, corners[1].uv), corners[2].uv);
  const float2 size = float2(length(axis_x.xyz), length(axis_y.xyz)) * (uv_max - uv_min);
  // Barycentric coordinates of the uv's middle.
  const float2 uv = 0.5f * (uv_min + uv_max);
  const float second = Cross(uv - corners[0].uv, along_second) / area;
  const float third = Cross(along_first, uv - corners[0].uv) / area;
  const float first = 1.f - second - third;
  const float3 centre = (corners[0].world.xyz * first) + (corners[1].world.xyz * second) + (corners[2].world.xyz * third);
  // The next cell's uv and its axes; the pixels' change along the axes from SV_POSITION's x, y and w.
  const float3 next = (corners[0].next * first) + (corners[1].next * second) + (corners[2].next * third);
  float4 next_x;
  float4 next_y;
  Axes(float4(corners[0].next.xy, 0.f, 0.f), float4(corners[1].next.xy, 0.f, 0.f), float4(corners[2].next.xy, 0.f, 0.f), along_first, along_second,
       area, next_x, next_y);
  float4 clip_x;
  float4 clip_y;
  Axes(corners[0].position, corners[1].position, corners[2].position, along_first, along_second, area, clip_x, clip_y);
  const float4 clip = (corners[0].position * first) + (corners[1].position * second) + (corners[2].position * third);
  const float2x2 per_pixel = PerPixel(clip, clip_x, clip_y, 0.5f * camera[29].xy);

  const Corner particle = corners[0];
  const int type = (int)floor(particle.kind.x + 0.1f);
  const uint bit = (type >= 0 && type < 32) ? (1u << (uint)type) : 0u;
  float alpha = 0.f;
  if ((LIT_TYPES & bit) != 0u) {
    const uint index = (uint)(int)floor(particle.kind.y + 0.1f);
    alpha = DiffuseAlpha(index, uv, float2(1.f, 0.f), float2(0.f, 1.f), per_pixel);
    if (next.x != 0.f) {
      alpha = lerp(alpha, DiffuseAlpha(index, next.xy, next_x.xy, next_y.xy, per_pixel), next.z);
    }
    alpha *= particle.colour.w;
    if ((WEIGHED_TYPES & bit) != 0u) alpha *= particle.size.w;
  }
  if ((CRUSH_TYPES & bit) != 0u) {
    const float threshold = saturate(particle.age.x);
    alpha = saturate(alpha - threshold) / (1.001f - threshold);
  }
  const bool around_camera = distance(centre, camera[0].xyz) < 0.5f * max(size.x, size.y);
  const bool placed = abs(area) > 1e-8f && all(isfinite(centre)) && size.x * size.y > 0.f;

  Record record;
  record.centre = float4(placed ? centre : asfloat(0x7FC00000u).xxx, particle.age.x);
  record.colour = float4(particle.colour.xyz, (placed && !around_camera) ? alpha : 0.f);
  record.size = float3(placed ? size : float2(0.f, 0.f), 1.f);
  record.emitter = float4(0.f, 0.f, 0.f, 0.f);
  records.Append(record);
}
