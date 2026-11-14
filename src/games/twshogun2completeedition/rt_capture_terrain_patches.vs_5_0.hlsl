/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Rarer Objects in the Rays (Experimental): Shogun 2's near battle terrain with the game's Tessellation on (terraintile
// vs30_infield_silouette_sm5, 0xC24D8705: 3-control-point patches, hull 0x54F0069F, domain 0x4FE62BBA) for ray tracing's
// stream output (twattila/rt_core.hpp, Host::scene_draw's capture version with SceneShader::patch_triangles). The vertex
// shader passes its world position on (POSITION0); each corner where the domain shader puts it without its silhouette
// smoothing: mul(float4(world, 1), view_projection), in clip space as the game's. Its vertex shader reads no constants: used
// only where the draw's vertex stage has the domain shader's camera_VS_PS at b0 (ray_tracing.hpp's SceneDraw checks).
cbuffer camera_VS_PS : register(b0)
{
  float4x4 view_projection : packoffset(c10);
}

void main(
  float4 v0 : POSITION0,
  out float4 position : SV_Position0)
{
  position = mul(float4(v0.xyz, 1.f), view_projection);
}
