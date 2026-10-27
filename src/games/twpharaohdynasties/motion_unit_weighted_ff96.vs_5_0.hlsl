// Attila unit motion (anti_aliasing.hpp): the game's 0xFF969484 with its unit's placement and bones last frame (motion.hlsli); outputs SV_Position as the game's,
// this frame's and last frame's clip positions without the jitter, its world rows and bone base for
// motion_unit.ps.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.

cbuffer camera : register(b0)
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
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
  float4 g_camera_temp0 : packoffset(c32);
  float4 g_camera_temp1 : packoffset(c33);
  float4 g_camera_temp2 : packoffset(c34);
  float4 g_clip_rect : packoffset(c35);
  float3 g_vr_head_rotation : packoffset(c36);
  int g_num_of_samples : packoffset(c36.w);
  float g_supersampling : packoffset(c37);
  float4 g_mouse_position : packoffset(c38);
  float3 g_frustum_points[8] : packoffset(c39);
  float g_orthographic : packoffset(c46.w);
  float g_overlay_lerp : packoffset(c47);
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
}

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_untextured : packoffset(c0.y);
  float g_snow : packoffset(c0.z);
  float g_rain : packoffset(c0.w);
  float g_tesselation_factor : packoffset(c1);
  float g_tesselation_displacement_scale : packoffset(c1.y);
  float2 g_uv2_tile_interval : packoffset(c1.z);
  float2 g_texel_density_uv_scale : packoffset(c2);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
  float g_amount_blendshapes[128] : packoffset(c5);
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
  float4 v13,
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
  inout float4 o11,
  inout uint4 o12)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(255.009995,255.009995) * v1.zy;
  r0.xy = floor(r0.xy);
  r0.xy = (int2)r0.xy;
  r0.z = 0.5 + g_animation_texture_width.x;
  r0.z = floor(r0.z);
  r0.w = 0.5 + v9.x;
  r0.w = floor(r0.w);
  r0.xy = (int2)r0.xy * int2(3,3);
  r0.xy = (uint2)r0.xy;
  r0.xy = r0.ww + r0.xy;
  r0.xyz = (uint3)r0.xyz;
  uiDest.xy = (uint2)r0.xy / (uint2)r0.zz;
  r1.xy = uiDest.xy;
  r0.xy = mad((int2)-r1.xy, (int2)r0.zz, (int2)r0.xy);
  r2.xyzw = (int4)r0.xxyy + int4(1,2,1,2);
  r0.z = r1.x;
  r0.w = 0;
  r3.xyzw = MotionAnimationLoad(r0.xzw).xyzw;
  r4.xw = r2.xy;
  r4.yz = r0.zw;
  r5.xyzw = MotionAnimationLoad(r4.xyz).xyzw;
  r4.xyzw = MotionAnimationLoad(r4.wyz).xyzw;
  r1.zw = r0.wy;
  r0.xyzw = MotionAnimationLoad(r1.wyz).xyzw;
  r1.xw = r2.zw;
  r2.xyzw = MotionAnimationLoad(r1.xyz).xyzw;
  r1.xyzw = MotionAnimationLoad(r1.wyz).xyzw;
  r6.x = 1 + -v1.x;
  r7.xyz = v0.xyz;
  r7.w = 1;
  r8.x = dot(r7.xyzw, r3.xyzw);
  r8.y = dot(r7.xyzw, r5.xyzw);
  r8.z = dot(r7.xyzw, r4.xyzw);
  r9.x = dot(r7.xyzw, r0.xyzw);
  r9.y = dot(r7.xyzw, r2.xyzw);
  r9.z = dot(r7.xyzw, r1.xyzw);
  r6.yzw = r9.xyz * r6.xxx;
  r7.xyz = v1.xxx * r8.xyz + r6.yzw;
  r6.yzw = v4.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r6.yzw, r6.yzw);
  r0.w = rsqrt(r0.w);
  r6.yzw = r6.yzw * r0.www;
  r8.x = dot(r6.yzw, r3.xyz);
  r8.y = dot(r6.yzw, r5.xyz);
  r8.z = dot(r6.yzw, r4.xyz);
  r9.x = dot(r6.yzw, r0.xyz);
  r9.y = dot(r6.yzw, r2.xyz);
  r9.z = dot(r6.yzw, r1.xyz);
  r6.yzw = r9.xyz * r6.xxx;
  r6.yzw = v1.xxx * r8.xyz + r6.yzw;
  r8.xyz = v2.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r8.xyz, r8.xyz);
  r0.w = rsqrt(r0.w);
  r8.xyz = r8.xyz * r0.www;
  r9.x = dot(r8.xyz, r3.xyz);
  r9.y = dot(r8.xyz, r5.xyz);
  r9.z = dot(r8.xyz, r4.xyz);
  r10.x = dot(r8.xyz, r0.xyz);
  r10.y = dot(r8.xyz, r2.xyz);
  r10.z = dot(r8.xyz, r1.xyz);
  r8.xyz = r10.xyz * r6.xxx;
  r8.xyz = v1.xxx * r9.xyz + r8.xyz;
  r9.xyz = v5.zyx * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r9.xyz, r9.xyz);
  r0.w = rsqrt(r0.w);
  r9.xyz = r9.xyz * r0.www;
  r3.x = dot(r9.xyz, r3.xyz);
  r3.y = dot(r9.xyz, r5.xyz);
  r3.z = dot(r9.xyz, r4.xyz);
  r0.x = dot(r9.xyz, r0.xyz);
  r0.y = dot(r9.xyz, r2.xyz);
  r0.z = dot(r9.xyz, r1.xyz);
  r0.xyz = r6.xxx * r0.xyz;
  r0.xyz = v1.xxx * r3.xyz + r0.xyz;
  if (g_pixel_selection_active != 0) {
    r1.x = (int)v14.x + g_pixel_selection_start_pixel_index;
    r1.yzw = float3(0,0,0);
    r0.w = t_pixel_selection_object_id_texture.Load(r1.xyz).x;
  }
  o12.x = g_pixel_selection_active ? r0.w : -1;
  r7.w = 1;
  r1.x = dot(r7.xyzw, v6.xyzw);
  r1.y = dot(r7.xyzw, v7.xyzw);
  r1.z = dot(r7.xyzw, v8.xyzw);
  r1.w = 1;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o3.x = dot(r8.xyz, v6.xyz);
  o3.y = dot(r8.xyz, v7.xyz);
  o3.z = dot(r8.xyz, v8.xyz);
  o6.x = dot(r6.yzw, v6.xyz);
  o6.y = dot(r6.yzw, v7.xyz);
  o6.z = dot(r6.yzw, v8.xyz);
  o4.x = dot(r0.xyz, v6.xyz);
  o4.y = dot(r0.xyz, v7.xyz);
  o4.z = dot(r0.xyz, v8.xyz);
  o1.xyzw = r1.xyzw;
  o2.xyzw = v3.xyxy;
  o3.w = v4.w;
  o4.w = v5.w;
  o5.xy = v3.xy;
  o5.z = v9.y;
  o5.w = v2.w;
  o6.w = v1.w;
  o7.xy = v9.zw;
  o7.zw = v10.xy;
  o8.xy = v10.zw;
  o8.zw = v13.xy;
  o9.xy = float2(1,1);
  o9.z = v11.y;
  o9.w = v13.z;
  o10.xy = v11.zw;
  o10.z = v13.w;
  o10.w = v0.y;
  o11.xyzw = v12.xyzw;
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
  float4 v13 : TEXCOORD7,
  uint v14 : SV_InstanceID0,
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
  float4 o9 = 0;
  float4 o10 = 0;
  float4 o11 = 0;
  uint4 o12 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 world = o0;
  // Last frame's placement of this mesh nearest this one, with last frame's bones when the pool has them (else the
  // current pose there).
  float4 rows[3] = {v6, v7, v8};
  float4 previous_rows[3];
  uint4 previous_bones;
  float4 previous_world = world;
  if (MotionPreviousPlacement(rows, previous_rows, previous_bones)) {
    motion_previous_entry = MotionPreviousBoneEntry(previous_bones.y);
    motion_previous_base = (int)floor(0.5f + asfloat(previous_bones.z));
    motion_current_base = (int)floor(0.5f + v9.x);
    const float4 previous_base_input = float4((motion_previous_entry != 0u ? asfloat(previous_bones.z) : v9.x), v9.yzw);
    motion_time = motion_times.x;  // last frame's (fur and the like sway with it)
    GameMain(v0, v1, v2, v3, v4, v5, previous_rows[0], previous_rows[1], previous_rows[2], previous_base_input, v10, v11, v12, v13, v14, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
    previous_world = o0;
    motion_previous_entry = 0u;
  }
  motion_row0 = v6;
  motion_row1 = v7;
  motion_row2 = v8;
  motion_bones = float4(v9.x, g_animation_texture_width.x, 0.f, 0.f);
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
