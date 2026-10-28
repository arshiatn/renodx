// Attila deformed in-draw motion (anti_aliasing.hpp): the game's 0x8D0D26B9 with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
// dxbc.hpp), this frame's and last frame's clip positions in render pixels (MOTION0 / MOTION1).
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}



// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float3 v0,
  float3 v1,
  float4 v2,
  float4 v3,
  float2 v4,
  inout float4 o0,
  inout float4 o1,
  inout float4 p1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = camera_position.xyz + -v0.xyz;
  r1.xyz = v1.yzx * r0.zxy;
  r1.xyz = r0.yzx * v1.zxy + -r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = r1.xyz * v3.xxx + v0.xyz;
  r2.w = 1;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o6.xyzw = r2.xyzw;
  p1.x = 0;
  r0.w = dot(v1.xyz, v1.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = v1.xyz * r0.www;
  r3.xyz = r2.zxy * r1.yzx;
  r3.xyz = r2.yzx * r1.zxy + -r3.xyz;
  o2.xyz = r1.xyz;
  o1.xyz = r3.xyz;
  o3.xyz = r2.xyz;
  o4.xy = v2.xy;
  o4.zw = v4.xy;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o5.z = dot(r0.xyz, r2.xyz);
  r0.x = abs(r3.y) * -0.0187292993 + 0.0742610022;
  r0.x = r0.x * abs(r3.y) + -0.212114394;
  r0.x = r0.x * abs(r3.y) + 1.57072878;
  r0.y = 1 + -abs(r3.y);
  r0.y = sqrt(r0.y);
  r0.z = r0.x * r0.y;
  r0.z = r0.z * -2 + 3.14159274;
  r0.w = cmp(r3.y < -r3.y);
  r1.x = cmp(r3.x < 0);
  r0.z = r0.w ? r0.z : 0;
  r0.x = r0.x * r0.y + r0.z;
  r0.y = 6.28318548 + -r0.x;
  r0.x = r1.x ? r0.y : r0.x;
  o5.x = 0.159154937 * r0.x;
  o5.y = v3.y;
  return;
}

void main(
  float3 v0 : POSITION0,
  float3 v1 : NORMAL0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float2 v4 : TEXCOORD2,
  uint motion_vertex_id : SV_VertexID,
  out float4 draw_o0 : SV_Position0,
  out float3 draw_o1 : TEXCOORD0,
  out float draw_p1 : TEXCOORD6,
  out float4 draw_o2 : TEXCOORD1,
  out float4 draw_o3 : TEXCOORD2,
  out float4 draw_o4 : TEXCOORD3,
  out float4 draw_o5 : TEXCOORD4,
  out float4 draw_o6 : TEXCOORD5,
  out float4 motion_now : MOTION0,
  out float4 motion_before : MOTION1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 p1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, o0, o1, p1, o2, o3, o4, o5, o6);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyz;
  draw_p1 = p1.x;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xyzw;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyzw;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  // Last frame's position of this vertex (motion.hlsli; none: this frame's).
  float4 previous_world;
  if (!MotionPreviousDeformed(motion_vertex_id - motion_draw.z, previous_world)) {
    GameMain(v0, v1, v2, v3, v4, o0, o1, p1, o2, o3, o4, o5, o6);
    previous_world = o0;
  }
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
