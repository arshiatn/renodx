// Attila's campaign plants and forests (the game's rigidcampaignvegetation vs30_main, 0x924D7813 and 0x819A414C) with their
// world position and UV for ray tracing's stream output (rt_core.hpp). Standing: the game leans each plant by the camera's
// height so that it shows from above; leaning with the camera its shadow would swing and its mesh change every frame.
#include "./rt_capture.hlsli"

void main(
  float4 v0 : POSITION0,
  float4 v1 : NORMAL0,
  float4 v2 : POSITION1,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0)
{
  const float3 local = v0.xyz * 0.4f;
  float turn_sin, turn_cos;
  sincos(v2.w, turn_sin, turn_cos);
  const float3 world = float3(local.x * turn_cos + local.z * turn_sin, local.y, local.z * turn_cos - local.x * turn_sin) + v2.xyz;
  world_uv = RtWorldUv(world, float2(v0.w, v1.w));
  position = 0.f;
}
