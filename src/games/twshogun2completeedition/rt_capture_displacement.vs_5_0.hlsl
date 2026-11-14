/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rarer Objects in the Rays (Experimental): Shogun 2's tessellated rigid objects (rigidstandard / rigidrainmesh
// vs30_displacement, 0x5074655A, with the game's Tessellation on: 3-control-point patches, hull 0x91442562, domain 0x94CEC8A9)
// for ray tracing's stream output (twattila/rt_core.hpp, Host::scene_draw's capture version with SceneShader::patch_triangles:
// the patches drawn as the triangles they are). Each corner where the domain shader puts it without displacement: the vertex
// shader's world position (rigidcommon.hlsl vs30_displacement: POSITION0 x the instance rows POSITION1-3), then the domain
// shader's mul(world, view_projection), in clip space as the game's; its UV (TEXCOORD0 in, the domain shader's TEXCOORD1 out,
// what an alpha-tested pixel shader of its reads) as the game's.
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  out float4 position : SV_Position0,
  out float4 uv : TEXCOORD1)
{
  const float4 local = float4(v0.xyz, 1.f);
  const float4 world = float4(dot(local, v6), dot(local, v7), dot(local, v8), 1.f);
  position = mul(world, view_projection);
  uv = v1;
}
