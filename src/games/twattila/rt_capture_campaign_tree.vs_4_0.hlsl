// Attila's campaign trees (the game's rigidcampaigntrees vs30_main, 0x4657DA8A) with their world position and UV for ray
// tracing's stream output (rt_core.hpp).
#include "./rt_capture.hlsli"

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : TEXCOORD1,
  float4 v7 : POSITION1,
  float4 v8 : POSITION2,
  float4 v9 : POSITION3,
  float4 v10 : POSITION4,
  float4 v11 : POSITION5,
  float4 v12 : POSITION6,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0)
{
  world_uv = RtWorldUv(float3(dot(v7, v0), dot(v8, v0), dot(v9, v0)), v1);
  position = 0.f;
}
