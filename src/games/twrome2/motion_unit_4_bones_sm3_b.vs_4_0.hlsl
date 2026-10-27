// Attila unit motion (anti_aliasing.hpp): the game's 0xEBF055A9 with its unit's placement and bones last frame (motion.hlsli); outputs SV_Position as the game's,
// this frame's and last frame's clip positions without the jitter, its world rows and bone base for
// motion_unit.ps.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view_projection : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4 camera_near_far : packoffset(c21);
  float time_in_sec : packoffset(c22);
  float2 g_inverse_focal_length : packoffset(c22.y);
  float g_vertical_fov : packoffset(c22.w);
  float4 g_screen_size : packoffset(c23);
  float g_vpos_texel_offset : packoffset(c24);
  float4 g_viewport_dimensions : packoffset(c25);
  float4 g_camera_temp0 : packoffset(c26);
  float4 g_camera_temp1 : packoffset(c27);
  float4 g_camera_temp2 : packoffset(c28);
}

cbuffer variant_animation_size : register(b1)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

SamplerState s_animation_map_s : register(s0);
Texture2D<float4> s_animation_map : register(t0);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// The animation texture: this frame's, or (motion_previous_entry) last frame's bones from the pool, relative to
// last frame's bone base (past the pool's: this frame's at the same place relative to this frame's).
static uint motion_previous_entry;
static int motion_previous_base;
static int motion_current_base;

float4 MotionAnimationLoad(int3 coords)
{
  if (motion_previous_entry == 0u) return s_animation_map.Load(coords);
  const int width = (int)floor(0.5f + g_animation_texture_width.x);
  const int texel_index = coords.y * width + coords.x - motion_previous_base;
  if (texel_index >= 0 && texel_index < (int)BONE_TEXELS) {
    return motion_previous_bones[(motion_previous_entry - 1u) * BONE_TEXELS + (uint)texel_index];
  }
  const int current = motion_current_base + texel_index;
  return s_animation_map.Load(int3(current % width, current / width, 0));
}

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float4 v3,
  float2 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(255.009995,255.009995,255.009995,255.009995) * v1.zyxw;
  r0.xyzw = floor(r0.xyzw);
  r0.xyzw = (int4)r0.xyzw;
  r0.xyzw = (int4)r0.xyzw * int4(3,3,3,3);
  r0.xyzw = (int4)r0.xyzw;
  r1.y = r0.y * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.x = g_animation_texture_width.y + r1.y;
  r2.w = g_animation_texture_height.y * v10.x;
  r1.w = r2.w;
  r3.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xw, 0).xyzw;
  r4.xyz = v0.xyz;
  r4.w = 1;
  r5.z = dot(r4.xyzw, r3.xyzw);
  r1.z = g_animation_texture_width.y * r0.y;
  r6.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.zw, 0).xyzw;
  r5.x = dot(r4.xyzw, r6.xyzw);
  r7.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yw, 0).xyzw;
  r5.y = dot(r4.xyzw, r7.xyzw);
  r5.xyz = v2.yyy * r5.xyz;
  r2.y = r0.x * g_animation_texture_width.y + g_animation_texture_width.y;
  r2.x = g_animation_texture_width.y + r2.y;
  r8.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.yw, 0).xyzw;
  r9.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.xw, 0).xyzw;
  r10.z = dot(r4.xyzw, r9.xyzw);
  r2.z = g_animation_texture_width.y * r0.x;
  r2.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r2.zw, 0).xyzw;
  r10.x = dot(r4.xyzw, r2.xyzw);
  r10.y = dot(r4.xyzw, r8.xyzw);
  r5.xyz = v2.zzz * r10.xyz + r5.xyz;
  r1.y = r0.z * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.x = g_animation_texture_width.y + r1.y;
  r10.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yw, 0).xyzw;
  r11.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xw, 0).xyzw;
  r12.z = dot(r4.xyzw, r11.xyzw);
  r1.z = g_animation_texture_width.y * r0.z;
  r13.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.zw, 0).xyzw;
  r12.x = dot(r4.xyzw, r13.xyzw);
  r12.y = dot(r4.xyzw, r10.xyzw);
  r0.xyz = v2.xxx * r12.xyz + r5.xyz;
  r2.w = v2.z + v2.y;
  r2.w = v2.x + r2.w;
  r2.w = 1 + -r2.w;
  r1.y = r0.w * g_animation_texture_width.y + g_animation_texture_width.y;
  r1.z = g_animation_texture_width.y * r0.w;
  r5.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.zw, 0).xyzw;
  r1.x = g_animation_texture_width.y + r1.y;
  r12.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.yw, 0).xyzw;
  r1.xyzw = s_animation_map.SampleLevel(s_animation_map_s, r1.xw, 0).xyzw;
  r14.z = dot(r4.xyzw, r1.xyzw);
  r14.x = dot(r4.xyzw, r5.xyzw);
  r14.y = dot(r4.xyzw, r12.xyzw);
  r0.xyz = r2.www * r14.xyz + r0.xyz;
  r0.w = 1;
  r4.x = dot(r0.xyzw, v7.xyzw);
  r4.y = dot(r0.xyzw, v8.xyzw);
  r4.z = dot(r0.xyzw, v9.xyzw);
  r4.w = 1;
  o0.x = dot(r4.xyzw, motion_rows[0]);
  o0.y = dot(r4.xyzw, motion_rows[1]);
  o0.z = dot(r4.xyzw, motion_rows[2]);
  o0.w = dot(r4.xyzw, motion_rows[3]);
  o1.xyzw = r4.xyzw;
  o2.xyzw = v4.xyxy;
  r0.xyz = v3.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r4.z = dot(r0.xyz, r3.xyz);
  r4.x = dot(r0.xyz, r6.xyz);
  r4.y = dot(r0.xyz, r7.xyz);
  r4.xyz = v2.yyy * r4.xyz;
  r14.z = dot(r0.xyz, r9.xyz);
  r14.x = dot(r0.xyz, r2.xyz);
  r14.y = dot(r0.xyz, r8.xyz);
  r4.xyz = v2.zzz * r14.xyz + r4.xyz;
  r14.z = dot(r0.xyz, r11.xyz);
  r14.x = dot(r0.xyz, r13.xyz);
  r14.y = dot(r0.xyz, r10.xyz);
  r4.xyz = v2.xxx * r14.xyz + r4.xyz;
  r14.z = dot(r0.xyz, r1.xyz);
  r14.x = dot(r0.xyz, r5.xyz);
  r14.y = dot(r0.xyz, r12.xyz);
  r0.xyz = r2.www * r14.xyz + r4.xyz;
  o3.x = dot(r0.xyz, v7.xyz);
  o3.y = dot(r0.xyz, v8.xyz);
  o3.z = dot(r0.xyz, v9.xyz);
  o3.w = v4.x;
  r0.xyz = v6.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r4.z = dot(r0.xyz, r3.xyz);
  r4.x = dot(r0.xyz, r6.xyz);
  r4.y = dot(r0.xyz, r7.xyz);
  r4.xyz = v2.yyy * r4.xyz;
  r14.z = dot(r0.xyz, r9.xyz);
  r14.x = dot(r0.xyz, r2.xyz);
  r14.y = dot(r0.xyz, r8.xyz);
  r4.xyz = v2.zzz * r14.xyz + r4.xyz;
  r14.z = dot(r0.xyz, r11.xyz);
  r14.x = dot(r0.xyz, r13.xyz);
  r14.y = dot(r0.xyz, r10.xyz);
  r4.xyz = v2.xxx * r14.xyz + r4.xyz;
  r14.z = dot(r0.xyz, r1.xyz);
  r14.x = dot(r0.xyz, r5.xyz);
  r14.y = dot(r0.xyz, r12.xyz);
  r0.xyz = r2.www * r14.xyz + r4.xyz;
  o4.x = dot(r0.xyz, v7.xyz);
  o4.y = dot(r0.xyz, v8.xyz);
  o4.z = dot(r0.xyz, v9.xyz);
  o4.w = v4.y;
  o5.xy = v4.xy;
  o5.z = v10.y;
  r0.xyz = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r3.z = dot(r0.xyz, r3.xyz);
  r3.x = dot(r0.xyz, r6.xyz);
  r3.y = dot(r0.xyz, r7.xyz);
  r3.xyz = v2.yyy * r3.xyz;
  r4.z = dot(r0.xyz, r9.xyz);
  r4.x = dot(r0.xyz, r2.xyz);
  r4.y = dot(r0.xyz, r8.xyz);
  r2.xyz = v2.zzz * r4.xyz + r3.xyz;
  r3.z = dot(r0.xyz, r11.xyz);
  r3.x = dot(r0.xyz, r13.xyz);
  r3.y = dot(r0.xyz, r10.xyz);
  r2.xyz = v2.xxx * r3.xyz + r2.xyz;
  r1.z = dot(r0.xyz, r1.xyz);
  r1.x = dot(r0.xyz, r5.xyz);
  r1.y = dot(r0.xyz, r12.xyz);
  r0.xyz = r2.www * r1.xyz + r2.xyz;
  o6.x = dot(r0.xyz, v7.xyz);
  o6.y = dot(r0.xyz, v8.xyz);
  o6.z = dot(r0.xyz, v9.xyz);
  o6.w = 1;
  o7.xy = v10.zw;
  o7.zw = v11.xy;
  o8.xy = v11.zw;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float2 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : TEXCOORD0,
  float4 v8 : TEXCOORD1,
  float4 v9 : TEXCOORD2,
  float4 v10 : TEXCOORD3,
  float4 v11 : TEXCOORD4,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1,
  out nointerpolation float4 motion_row0 : TEXCOORD2,
  out nointerpolation float4 motion_row1 : TEXCOORD3,
  out nointerpolation float4 motion_row2 : TEXCOORD4,
  out nointerpolation float4 motion_bones : TEXCOORD5)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float4 o8 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  const float4 world = o0;
  // Last frame's placement of this mesh nearest this one, with last frame's bones when the pool has them (else the
  // current pose there).
  float4 rows[3] = {v7, v8, v9};
  float4 previous_rows[3];
  uint4 previous_bones;
  float4 previous_world = world;
  if (MotionPreviousPlacement(rows, previous_rows, previous_bones)) {
    motion_previous_entry = MotionPreviousBoneEntry(previous_bones.y);
    motion_previous_base = (int)floor(0.5f + asfloat(previous_bones.z));
    motion_current_base = (int)floor(0.5f + v10.x);
    const float4 previous_base_input = float4((motion_previous_entry != 0u ? asfloat(previous_bones.z) : v10.x), v10.yzw);
    motion_time = motion_times.x;  // last frame's (fur and the like sway with it)
    GameMain(v0, v1, v2, v3, v4, v5, v6, previous_rows[0], previous_rows[1], previous_rows[2], previous_base_input, v11, o0, o1, o2, o3, o4, o5, o6, o7, o8);
    previous_world = o0;
    motion_previous_entry = 0u;
  }
  motion_row0 = v7;
  motion_row1 = v8;
  motion_row2 = v9;
  motion_bones = float4(v10.x, 0.f, 0.f, 0.f);
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
