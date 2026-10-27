// Attila wind motion (anti_aliasing.hpp): the game's 0x308BB4D5 with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
// and last frame's clip positions without the jitter.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.
// Translated from the disassembly by tools\tw\attila_motion\asm_to_hlsl.py (3Dmigoto's decompiler crashed on it).
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
}

cbuffer wind_VS : register(b1)
{
  float2 wind_direction : packoffset(c0);
  float gust_start_time : packoffset(c0.z);
  float wind_vegetation_multiplier : packoffset(c0.w);
  float wind_vegetation_hstrength : packoffset(c1);
  float wind_vegetation_vstrength : packoffset(c1.y);
}

cbuffer rigid_config_PS : register(b2)
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

Texture2D<uint4> t_pixel_selection_object_id_texture : register(t0);

#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float2 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float4 v13,
  float3 v14,
  uint v15,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 p2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout uint4 o6)
{
  float4 r0, r1, r2;
  o0 = 0;
  o1 = 0;
  o2 = 0;
  p2 = 0;
  o3 = 0;
  o4 = 0;
  o5 = 0;
  o6 = 0;
  r0.x = dot(v8.xyzw, v0.xyzw);
  r0.y = dot(v9.xyzw, v0.xyzw);
  r0.z = dot(v10.xyzw, v0.xyzw);
  r1.x = dot(v11.xyzw, v1.xyzw);
  r1.y = dot(v12.xyzw, v1.xyzw);
  r1.z = dot(v13.xyzw, v1.xyzw);
  r2.x = asfloat(((0.0 < v6.y) ? 0xffffffffu : 0u));
  r2.y = asfloat(((v6.y < 1.0) ? 0xffffffffu : 0u));
  r2.x = asfloat((asuint(r2.y) & asuint(r2.x)));
  r2.y = (-(v6.y) + 1.0);
  r2.x = saturate(((asint(r2.x) != 0) ? r2.y : v6.y));
  r0.w = v0.w;
  r1.w = v1.w;
  r1.xyzw = float4((-(r0.x) + r1.x), (-(r0.y) + r1.y), (-(r0.z) + r1.z), (-(r0.w) + r1.w));
  r0.xyzw = float4((r2.x * r1.x + r0.x), (r2.x * r1.y + r0.y), (r2.x * r1.z + r0.z), (r2.x * r1.w + r0.w));
  r1.x = (v14.x * 4.0 + motion_time);
  r1.yz = float2((motion_time * 3.0), (motion_time * 0.5));
  {
    const float together0_0 = sin(r1.x);
    const float together0_1 = sin(r1.y);
    const float together0_2 = sin(r1.z);
    r1.xyz = float3(together0_0, together0_1, together0_2);
  }
  r1.x = (r1.y * r1.x);
  r1.x = (r1.x * wind_vegetation_hstrength);
  r1.x = (r1.x * 0.5 + 1.0);
  r1.y = (r1.z * 0.5 + 0.75);
  r1.x = (r1.y * r1.x);
  r1.y = (v14.x * 2.0 + motion_time);
  r1.y = (r1.y * 0.30000001192092896);
  {
    const float together0_0 = sin(r1.y);
    r1.y = together0_0;
  }
  r1.xy = float2((r1.x * wind_vegetation_hstrength), (r1.y * wind_vegetation_vstrength));
  r1.z = (r0.x + motion_time);
  r1.z = (r1.z * 6.0);
  {
    const float together0_0 = sin(r1.z);
    r1.z = together0_0;
  }
  r1.y = (r1.z * r1.y);
  r1.z = saturate((v7.w + -0.699999988079071));
  r1.z = (r1.z * wind_vegetation_multiplier);
  r1.w = (v7.w * wind_vegetation_multiplier);
  r1.w = (r1.w * v7.w);
  r2.xy = float2((r1.w * wind_direction.x), (r1.w * wind_direction.y));
  r2.xz = float2((r1.x * r2.x), (r1.x * r2.y));
  r2.y = asfloat(0u);
  r2.xyz = float3((r0.x + r2.x), (r0.y + r2.y), (r0.z + r2.z));
  r1.x = (r1.y * r1.z);
  r0.xyz = float3((r1.x * 0.0 + r2.x), (r1.x * 3.3333330154418945 + r2.y), (r1.x * 0.0 + r2.z));
  o0.x = dot(r0.xyzw, motion_rows[0]);
  o0.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  r0.w = dot(r0.xyzw, motion_rows[3]);
  o3.w = (-(v14.y) + 1.0);
  o3.x = dot(v8.xyzw, v2.xyzw);
  o3.y = dot(v9.xyzw, v2.xyzw);
  o3.z = dot(v10.xyzw, v2.xyzw);
  o4.x = dot(v8.xyzw, v3.xyzw);
  o4.y = dot(v9.xyzw, v3.xyzw);
  o4.z = dot(v10.xyzw, v3.xyzw);
  o5.x = dot(v8.xyzw, v4.xyzw);
  o5.y = dot(v9.xyzw, v4.xyzw);
  o5.z = dot(v10.xyzw, v4.xyzw);
  if (g_pixel_selection_active != 0) {
    r1.x = asfloat((asint(v15) + g_pixel_selection_start_pixel_index));
    r1.yzw = float3(asfloat(0u), asfloat(0u), asfloat(0u));
    r1.xyzw = float4(asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r1.x), asint(r1.y), asint(r1.w))).x), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r1.x), asint(r1.y), asint(r1.w))).y), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r1.x), asint(r1.y), asint(r1.w))).z), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r1.x), asint(r1.y), asint(r1.w))).w));
  }
  o6 = asuint(((g_pixel_selection_active != 0) ? r1.x : asfloat(4294967295u)));
  o0.w = r0.w;
  o1.xyzw = float4(r0.x, r0.y, r0.z, r0.w);
  o2.xy = float2(v5.x, v5.y);
  o2.z = v14.x;
  p2 = v14.z;
  o4.w = v7.w;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float2 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : POSITION7,
  float4 v8 : TEXCOORD0,
  float4 v9 : TEXCOORD1,
  float4 v10 : TEXCOORD2,
  float4 v11 : TEXCOORD3,
  float4 v12 : TEXCOORD4,
  float4 v13 : TEXCOORD5,
  float3 v14 : TEXCOORD6,
  uint v15 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 p2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  uint4 o6 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, p2, o3, o4, o5, o6);
  const float4 world = o0;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, p2, o3, o4, o5, o6);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
