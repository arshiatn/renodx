// Attila grass rows (rt_scene.hpp): the instance rows of the game's grass (0xF3741248, 0x01FA9CDE: one input signature)
// for ray tracing's placements, one point per instance through stream output. The grass past its share of the frame goes
// in placed (still, its mesh's BLAS from its first capture) instead of left out (ATN 2026-10-29: with Grass Always Drawn
// whole fields lost their shadows). Its rows carry its blades' shrink with distance (rt_grass.hlsli), as its capture
// versions shrink the copy its mesh is built from.
#include "./rt_grass.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : COLOR0,
  float4 v5 : TEXCOORD1,
  float4 v6 : TEXCOORD2,
  float4 v7 : TEXCOORD3,
  float2 v8 : TEXCOORD4,
  out float4 row0 : TEXCOORD0,
  out float4 row1 : TEXCOORD1,
  out float4 row2 : TEXCOORD2,
  out uint4 record : TEXCOORD3)
{
  // The rows times the shrink's: y' = shrink (y + 0.6) - 0.6.
  const float shrink = RtGrassShrink(float3(v5.w, v6.w, v7.w), camera_position);
  const float4 scale = float4(1, shrink, 1, 1);
  const float lift = 0.6 * (shrink - 1);
  row0 = v5 * scale + float4(0, 0, 0, lift * v5.y);
  row1 = v6 * scale + float4(0, 0, 0, lift * v6.y);
  row2 = v7 * scale + float4(0, 0, 0, lift * v7.y);
  record = 0u;
}
