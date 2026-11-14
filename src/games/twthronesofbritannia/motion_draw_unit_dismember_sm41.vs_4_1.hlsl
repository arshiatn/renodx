// Attila unit in-draw motion (anti_aliasing.hpp): the game's 0xC9F9DBA5 with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
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

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_dissolve_enable : packoffset(c0.y);
  float g_untextured : packoffset(c0.z);
  float g_snow : packoffset(c0.w);
  float g_rain : packoffset(c1);
  float g_tesselation_factor : packoffset(c1.y);
  float g_tesselation_displacement_scale : packoffset(c1.z);
  float2 g_uv2_tile_interval : packoffset(c2);
  float2 g_texel_density_uv_scale : packoffset(c2.z);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
}

cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

Texture2D<float4> t_animation_map : register(t0);
Texture2D<uint> t_pixel_selection_object_id_texture : register(t1);


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
  if (motion_previous_entry == 0u) return t_animation_map.Load(coords);
  const int width = (int)floor(0.5f + g_animation_texture_width.x);
  const int texel_index = coords.y * width + coords.x - motion_previous_base;
  if (texel_index >= 0 && texel_index < (int)BONE_TEXELS) {
    return motion_previous_bones[(motion_previous_entry - 1u) * BONE_TEXELS + (uint)texel_index];
  }
  const int current = motion_current_base + texel_index;
  return t_animation_map.Load(int3(current % width, current / width, 0));
}

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float2 v3,
  float4 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float2 v13,
  uint v14,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8,
  inout float4 o9,
  inout float4 o10,
  inout uint4 p10,
  inout float4 o11)
{
  const float4 icb[] = { { 1.000000, 0, 0, 0},
                              { 0, 1.000000, 0, 0},
                              { 0, 0, 1.000000, 0},
                              { 0, 0, 0, 1.000000} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v1.zy * float2(255,255) + float2(0.5,0.5);
  r0.xy = floor(r0.xy);
  r0.z = 0.5 + g_animation_texture_width.x;
  r0.z = floor(r0.z);
  r0.xyz = (int3)r0.xyz;
  r0.w = 0.5 + v9.x;
  r0.w = floor(r0.w);
  r1.xy = (int2)r0.xy * int2(3,3);
  r1.xy = (int2)r1.xy;
  r1.xy = r1.xy + r0.ww;
  r1.xy = (int2)r1.xy;
  r1.zw = (int2)r0.zz ^ (int2)r1.xy;
  r2.xy = max((int2)-r1.xy, (int2)r1.xy);
  r0.w = max((int)-r0.z, (int)r0.z);
  uiDest.xy = (uint2)r2.xy / (uint2)r0.ww;
  r2.xy = uiDest.xy;
  r2.zw = -(int2)r2.xy;
  r1.zw = (int2)r1.zw & int2(0x80000000,0x80000000);
  r2.xy = r1.zw ? r2.zw : r2.xy;
  r1.xy = mad((int2)-r2.xy, (int2)r0.zz, (int2)r1.xy);
  r3.xyzw = (int4)r1.xxyy + int4(1,2,1,2);
  r1.z = r2.x;
  r1.w = 0;
  r4.xyzw = MotionAnimationLoad(r1.xzw).xyzw;
  r5.xw = r3.xy;
  r5.yz = r1.zw;
  r6.xyzw = MotionAnimationLoad(r5.xyz).xyzw;
  r5.xyzw = MotionAnimationLoad(r5.wyz).xyzw;
  r2.zw = r1.wy;
  r1.xyzw = MotionAnimationLoad(r2.wyz).xyzw;
  r2.xw = r3.zw;
  r3.xyzw = MotionAnimationLoad(r2.xyz).xyzw;
  r2.xyzw = MotionAnimationLoad(r2.wyz).xyzw;
  r7.x = cmp(0.100000001 < v11.x);
  if (r7.x != 0) {
    r7.x = v11.x * 3 + v9.x;
    r7.x = (int)r7.x;
    r7.yz = (int2)r0.xy ^ int2(4,4);
    r8.xy = max((int2)-r0.xy, (int2)r0.xy);
    r8.zw = (uint2)r8.xy >> 2;
    r9.xy = -(int2)r8.zw;
    r7.yz = (int2)r7.yz & int2(0x80000000,0x80000000);
    r7.yz = r7.yz ? r9.xy : r8.zw;
    r7.xy = (int2)r7.yz + (int2)r7.xx;
    r7.zw = (int2)r0.zz ^ (int2)r7.xy;
    r8.zw = max((int2)-r7.xy, (int2)r7.xy);
    uiDest.zw = (uint2)r8.zw / (uint2)r0.ww;
    r8.zw = uiDest.zw;
    r9.xy = -(int2)r8.zw;
    r7.zw = (int2)r7.zw & int2(0x80000000,0x80000000);
    r9.xy = r7.zw ? r9.xy : r8.zw;
    r7.xy = mad((int2)-r9.xy, (int2)r0.zz, (int2)r7.xy);
    r7.z = r9.x;
    r7.w = 0;
    r10.xyzw = MotionAnimationLoad(r7.xzw).xyzw;
    r0.xy = (int2)r0.xy & int2(0x80000000,0x80000000);
    r0.zw = (int2)r8.xy & int2(3,3);
    r7.xz = -(int2)r0.zw;
    r0.xy = r0.xy ? r7.xz : r0.zw;
    r0.x = dot(r10.xyzw, icb[r0.x+0].xyzw);
    r9.zw = r7.yw;
    r7.xyzw = MotionAnimationLoad(r9.zyw).xyzw;
    r0.y = dot(r7.xyzw, icb[r0.y+0].xyzw);
    r0.zw = saturate(r0.yx);
    r0.w = r0.w + -r0.z;
    r7.x = v1.x * r0.w + r0.z;
    r0.x = r0.x + -r0.y;
    r7.y = v1.x * r0.x + r0.y;
  } else {
    r7.xy = float2(1,1);
  }
  r0.x = 1 + -v1.x;
  r8.xyz = v0.xyz;
  r8.w = 1;
  r9.x = dot(r8.xyzw, r4.xyzw);
  r9.y = dot(r8.xyzw, r6.xyzw);
  r9.z = dot(r8.xyzw, r5.xyzw);
  r10.x = dot(r8.xyzw, r1.xyzw);
  r10.y = dot(r8.xyzw, r3.xyzw);
  r10.z = dot(r8.xyzw, r2.xyzw);
  r0.yzw = r10.xyz * r0.xxx;
  r8.xyz = v1.xxx * r9.xyz + r0.yzw;
  r0.yzw = v4.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r9.x = dot(r0.yzw, r4.xyz);
  r9.y = dot(r0.yzw, r6.xyz);
  r9.z = dot(r0.yzw, r5.xyz);
  r10.x = dot(r0.yzw, r1.xyz);
  r10.y = dot(r0.yzw, r3.xyz);
  r10.z = dot(r0.yzw, r2.xyz);
  r0.yzw = r10.xyz * r0.xxx;
  r0.yzw = v1.xxx * r9.xyz + r0.yzw;
  r9.xyz = v2.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r10.x = dot(r9.xyz, r4.xyz);
  r10.y = dot(r9.xyz, r6.xyz);
  r10.z = dot(r9.xyz, r5.xyz);
  r11.x = dot(r9.xyz, r1.xyz);
  r11.y = dot(r9.xyz, r3.xyz);
  r11.z = dot(r9.xyz, r2.xyz);
  r9.xyz = r11.xyz * r0.xxx;
  r9.xyz = v1.xxx * r10.xyz + r9.xyz;
  r10.xyz = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r4.x = dot(r10.xyz, r4.xyz);
  r4.y = dot(r10.xyz, r6.xyz);
  r4.z = dot(r10.xyz, r5.xyz);
  r1.x = dot(r10.xyz, r1.xyz);
  r1.y = dot(r10.xyz, r3.xyz);
  r1.z = dot(r10.xyz, r2.xyz);
  r1.xyz = r1.xyz * r0.xxx;
  r1.xyz = v1.xxx * r4.xyz + r1.xyz;
  r2.x = (int)v14.x + g_pixel_selection_start_pixel_index;
  r2.yzw = float3(0,0,0);
  r0.x = t_pixel_selection_object_id_texture.Load(r2.xyz).x;
  r8.w = 1;
  r2.x = dot(r8.xyzw, v6.xyzw);
  r2.y = dot(r8.xyzw, v7.xyzw);
  r2.z = dot(r8.xyzw, v8.xyzw);
  r2.w = 1;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o3.x = dot(r9.xyz, v6.xyz);
  o3.y = dot(r9.xyz, v7.xyz);
  o3.z = dot(r9.xyz, v8.xyz);
  o6.x = dot(r0.yzw, v6.xyz);
  o6.y = dot(r0.yzw, v7.xyz);
  o6.z = dot(r0.yzw, v8.xyz);
  o4.x = dot(r1.xyz, v6.xyz);
  o4.y = dot(r1.xyz, v7.xyz);
  o4.z = dot(r1.xyz, v8.xyz);
  o1.xyzw = r2.xyzw;
  o2.xyzw = v3.xyxy;
  o3.w = v3.x;
  o4.w = v3.y;
  o6.w = 1;
  o7.xy = v9.zw;
  o7.zw = v10.xy;
  o8.xy = v10.zw;
  o8.zw = v13.xy;
  o11.xyzw = v12.xyzw;
  o5.xy = v3.xy;
  o5.z = v9.y;
  r7.z = v11.y;
  o9.xyz = r7.xyz;
  o10.xy = v11.zw;
  p10.x = r0.x;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float2 v3 : POSITION3,
  float4 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v6 : TEXCOORD0,
  float4 v7 : TEXCOORD1,
  float4 v8 : TEXCOORD2,
  float4 v9 : TEXCOORD3,
  float4 v10 : TEXCOORD4,
  float4 v11 : TEXCOORD5,
  float4 v12 : TEXCOORD6,
  float2 v13 : TEXCOORD7,
  uint v14 : SV_InstanceID0,
  out float4 draw_o0 : SV_Position0,
  out float4 draw_o1 : TEXCOORD0,
  out float4 draw_o2 : TEXCOORD1,
  out float4 draw_o3 : TEXCOORD2,
  out float4 draw_o4 : TEXCOORD3,
  out float4 draw_o5 : TEXCOORD4,
  out float4 draw_o6 : TEXCOORD5,
  out float4 draw_o7 : TEXCOORD6,
  out float4 draw_o8 : TEXCOORD7,
  out float4 draw_o9 : TEXCOORD8,
  out float2 draw_o10 : TEXCOORD9,
  out float draw_p10 : POSITION2,
  out float4 draw_o11 : POSITION1,
  out float4 motion_now : MOTION0,
  out float4 motion_before : MOTION1)
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
  float4 o9 = 0;
  float4 o10 = 0;
  uint4 p10 = 0;
  float4 o11 = 0;
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, p10, o11);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyzw;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xyzw;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyzw;
  draw_o7 = o7.xyzw;
  draw_o8 = o8.xyzw;
  draw_o9 = o9.xyzw;
  draw_o10 = o10.xy;
  draw_p10 = asfloat(p10.x);
  draw_o11 = o11.xyzw;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  // Last frame's placement of this mesh nearest this one, with last frame's bones when the pool has them (else
  // this frame's placement and pose).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  uint4 previous_bones;
  float4 previous_base_input = v9;
  motion_previous_entry = 0u;
  if (MotionPreviousPlacement(rows, previous_rows, previous_bones)) {
    motion_previous_entry = MotionPreviousBoneEntry(previous_bones.y);
    motion_previous_base = (int)floor(0.5f + asfloat(previous_bones.z));
    motion_current_base = (int)floor(0.5f + v9.x);
    previous_base_input.x = (motion_previous_entry != 0u ? asfloat(previous_bones.z) : v9.x);
  }
  motion_time = motion_times.x;  // last frame's (fur and the like sway with it)
  GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], previous_base_input, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, p10, o11);
  const float4 previous_world = o0;
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
