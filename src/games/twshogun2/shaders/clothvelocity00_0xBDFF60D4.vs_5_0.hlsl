// cloth.hlsl vs30_main: banners and flags (simulated on the CPU: world-space positions, the previous step's as
// prev_position for the motion vectors).
// Paused, the simulation stops but prev_position keeps the last step's: a still banner kept that step's motion, up to
// hundreds of pixels after a jump (ATN debug16, paused: a near banner pointed 300-590 px away, a far flag 1 px), and
// DLSS dropped or smeared its history every frame. RenoDX: while the scene's time stands still (time_in_sec ==
// prev_time_in_sec, as paused) the previous position is the current one, within a millisecond.
// v34: the positions are homogeneous: the units' banners (UI, hidden with K) are scaled by their w (debug19, paused,
// time - prev time exactly 0: with the previous position = the current one the banner still moved up to 33000 px), and
// the pixel shader's motion vector projects xyz with w = 1. Divided by w here (0: as the game).

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
}

cbuffer cloth_VS : register(b1)
{
  float4x4 g_world : packoffset(c0);
}

void main(
  float4 position : POSITION0,
  float3 normal : TEXCOORD0,
  float2 texture_uv : TEXCOORD1,
  float4 prev_position : TEXCOORD2,
  out float4 o_position : SV_Position0,
  out float3 o_eye_dir : TEXCOORD0,
  out float4 o_world_position : TEXCOORD1,
  out float3 o_world_normal : TEXCOORD2,
  out float2 o_texture_uv : TEXCOORD3,
  out float4 o_clip_position : TEXCOORD4,
  out float4 o_eye_position : TEXCOORD5,
  out float4 o_prev_world_position : TEXCOORD6)
{
  o_position = mul(position, view_projection);
  o_eye_dir = normalize(camera_position - position.xyz);
  o_world_position = position;
  o_world_normal = mul(normalize(normal), (float3x3)g_world);
  o_texture_uv = texture_uv;
  o_clip_position = o_position;
  o_eye_position = mul(position, view);
  const float4 previous = (abs(time_in_sec - prev_time_in_sec) < 1e-3f) ? position : prev_position;
  o_prev_world_position = float4(previous.xyz / ((previous.w != 0.f) ? previous.w : 1.f), 1.f);
}
