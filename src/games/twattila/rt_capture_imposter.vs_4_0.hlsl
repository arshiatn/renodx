// Attila's sprite soldiers (the game's imposter.hlsl vs30_main, 0x1B9E7692) with their world position and UV for ray
// tracing's stream output (rt_core.hpp, SceneShader::sun_facing): the card turned to the sun instead of the camera, so its
// shadow keeps its width and does not swing with the view. t0: rt_sun, its first texel the direction toward the sun.
#include "./rt_capture.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
}

Texture2D<float4> t_rt_sun : register(t0);

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  float2 v6 : TEXCOORD3,
  out float4 position : SV_Position0,
  out float4 world_uv : TEXCOORD0)
{
  const float3 toward_sun = t_rt_sun.Load(int3(0, 0, 0)).xyz;
  const float across = length(toward_sun.xz);
  const float3 right = (across > 1e-3f) ? float3(-toward_sun.z, 0.f, toward_sun.x) / across : float3(1.f, 0.f, 0.f);

  // The game's card (standing, or flat on the ground with v1.w below 0), its right vector replaced.
  float3 standing = right * v2.z + v0.xyz;
  standing.y += v3.w;
  const float3 flat_card = right * v3.y + float3(standing.x, standing.y - v3.z, standing.z);
  const bool stands = v1.w >= 0.f;
  const float3 world = stands ? standing : flat_card;

  // The game's UV.
  float4 r0, r1;
  r0.x = 1.f;
  r0.y = -v6.y;
  r0.z = -v0.w * v6.x + v1.x;
  r1.x = v6.x * v1.w;
  r1.x = -r1.x * 8.f + r0.z;
  r0.z = -v6.x * 0.99000001f + r1.x;
  r1.z = floor(-r0.z);
  const bool wraps = r0.z < 0.f;
  r1.z = 1.f + r1.z;
  r1.y = v1.y;
  r0.xy = r0.xy * r1.zz + r1.xy;
  r0.xy = wraps ? r0.xy : r1.xy;
  r1.xy = v6.xy * v2.xy;
  r0.xy = -r1.xy * float2(0.980000019f, 0.99000001f) + r0.xy;
  r0.y = v3.z + r0.y;
  r0.z = v6.y + r0.y;
  const float2 flat_uv = float2(r1.x * 0.980000019f + v1.x, -r1.y * 0.99000001f + v1.y);
  const float2 uv = stands ? r0.xz : flat_uv;

  world_uv = RtWorldUv(world, uv);
  position = 0.f;
}
