// Attila world motion (anti_aliasing.hpp): the game's 0xCB04CFA7 with its world position for ray tracing's stream output
// (rt_scene.hpp).
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

cbuffer texture_scale : register(b2)
{
  float4 g_texture0_offset_scale : packoffset(c0);
  float4 g_texture1_offset_scale : packoffset(c1);
  float4 g_texture2_offset_scale : packoffset(c2);
  float4 g_texture3_offset_scale : packoffset(c3);
  float4 g_texture4_offset_scale : packoffset(c4);
  float4 g_texture5_offset_scale : packoffset(c5);
  float4 g_texture6_offset_scale : packoffset(c6);
  float4 g_texture7_offset_scale : packoffset(c7);
  float wind_offset : packoffset(c8);
}

cbuffer texture_blend_PS : register(b3)
{
  float2 texture0_blend_height : packoffset(c0);
  float2 texture1_blend_height : packoffset(c0.z);
  float2 texture2_blend_height : packoffset(c1);
  float2 texture3_blend_height : packoffset(c1.z);
  float2 texture4_blend_height : packoffset(c2);
  float2 texture5_blend_height : packoffset(c2.z);
  float2 texture6_blend_height : packoffset(c3);
  float2 texture7_blend_height : packoffset(c3.z);
  float3 colour0 : packoffset(c4);
  float3 colour1 : packoffset(c5);
  float3 colour2 : packoffset(c6);
  float3 colour3 : packoffset(c7);
  float3 colour4 : packoffset(c8);
  float3 colour5 : packoffset(c9);
  float3 colour6 : packoffset(c10);
  float3 colour7 : packoffset(c11);
  float global_alpha0 : packoffset(c11.w);
  float global_alpha1 : packoffset(c12);
  float global_alpha2 : packoffset(c12.y);
  float global_alpha3 : packoffset(c12.z);
  float global_alpha4 : packoffset(c12.w);
  float global_alpha5 : packoffset(c13);
  float global_alpha6 : packoffset(c13.y);
  float global_alpha7 : packoffset(c13.z);
  float height0_vertical : packoffset(c13.w);
  float height1_vertical : packoffset(c14);
  float height2_vertical : packoffset(c14.y);
  float height3_vertical : packoffset(c14.z);
  float height4_vertical : packoffset(c14.w);
  float height5_vertical : packoffset(c15);
  float height6_vertical : packoffset(c15.y);
  float height7_vertical : packoffset(c15.z);
  float3 fake_ui_light_direction : packoffset(c16);
  float4 g_texture0_output_offset_scale : packoffset(c17);
  float4 g_texture1_output_offset_scale : packoffset(c18);
  float4 g_texture2_output_offset_scale : packoffset(c19);
  float4 g_texture3_output_offset_scale : packoffset(c20);
  float4 g_texture4_output_offset_scale : packoffset(c21);
  float4 g_texture5_output_offset_scale : packoffset(c22);
  float4 g_texture6_output_offset_scale : packoffset(c23);
  float4 g_texture7_output_offset_scale : packoffset(c24);
}

Texture2D<uint> t_pixel_selection_object_id_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float2 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  uint v10,
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
  inout uint4 o11)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r2.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  r3.xyz = v0.xyz;
  r3.w = 1;
  r4.x = dot(r3.xyzw, v6.xyzw);
  r4.y = dot(r3.xyzw, v7.xyzw);
  r4.z = dot(r3.xyzw, v8.xyzw);
  r3.x = dot(r0.xyz, v6.xyz);
  r3.y = dot(r0.xyz, v7.xyz);
  r3.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r3.xyz, r3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r3.xyz * r0.xxx;
  r3.x = dot(r1.xyz, v6.xyz);
  r3.y = dot(r1.xyz, v7.xyz);
  r3.z = dot(r1.xyz, v8.xyz);
  r0.w = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.w);
  o3.xyz = r3.xyz * r0.www;
  r1.x = dot(r2.xyz, v6.xyz);
  r1.y = dot(r2.xyz, v7.xyz);
  r1.z = dot(r2.xyz, v8.xyz);
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  o4.xyz = r1.xyz * r0.www;
  r0.w = wind_offset + motion_time;
  r1.x = r0.w * 2 + r4.y;
  r1.x = sin(r1.x);
  r1.yz = float2(-2.70000005,4.19999981) + r0.ww;
  r1.xyz = float3(0.400000006,0.600000024,1.60000002) * r1.xyz;
  r2.xy = sin(r1.yz);
  r2.z = r2.x * r1.x;
  r1.x = 4 * r0.w;
  r1.xy = r4.yy * float2(5,15) + r1.xx;
  r1.xy = sin(r1.xy);
  r1.xy = float2(0.0800000057,0.0200000014) * r1.xy;
  r0.w = sin(r0.w);
  r0.w = r1.x * r0.w;
  r3.xz = float2(0.600000024,0.600000024) * r0.ww;
  r3.y = 0;
  r0.w = r1.y * r2.y;
  r1.xz = float2(0.600000024,0.600000024) * r0.ww;
  r1.y = 0;
  r2.xyz = r2.xxz * float3(0,0,0.600000024) + r3.xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r1.xyz = v5.www * r1.xyz;
  r0.w = 0.800000012 + -v5.y;
  r1.w = dot(r1.xz, r1.xz);
  r1.w = rsqrt(r1.w);
  r2.xy = r1.xz * r1.ww;
  r1.w = saturate(dot(r0.xz, -r2.xy));
  r2.xyz = r1.xyz * r0.www + -r1.xyz;
  r1.xyz = r1.www * r2.xyz + r1.xyz;
  r1.xyz = r1.xyz * v9.xxx + r4.xyz;
  r1.w = 1;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o4.w = dot(r1.xyzw, view._m02_m12_m22_m32);
  r2.xy = -g_texture0_output_offset_scale.xy + v1.xy;
  o7.xy = r2.xy / g_texture0_output_offset_scale.zw;
  r2.xy = -g_texture1_output_offset_scale.xy + v1.xy;
  o7.zw = r2.xy / g_texture1_output_offset_scale.zw;
  r2.xy = -g_texture2_output_offset_scale.xy + v1.xy;
  o8.xy = r2.xy / g_texture2_output_offset_scale.zw;
  r2.xy = -g_texture3_output_offset_scale.xy + v1.xy;
  o8.zw = r2.xy / g_texture3_output_offset_scale.zw;
  r2.xy = -g_texture4_output_offset_scale.xy + v1.xy;
  o9.xy = r2.xy / g_texture4_output_offset_scale.zw;
  r2.xy = -g_texture5_output_offset_scale.xy + v1.xy;
  o9.zw = r2.xy / g_texture5_output_offset_scale.zw;
  r2.xy = -g_texture6_output_offset_scale.xy + v1.xy;
  o10.xy = r2.xy / g_texture6_output_offset_scale.zw;
  r2.xy = -g_texture7_output_offset_scale.xy + v1.xy;
  o10.zw = r2.xy / g_texture7_output_offset_scale.zw;
  if (g_pixel_selection_active != 0) {
    r2.x = (int)v10.x + g_pixel_selection_start_pixel_index;
    r2.yzw = float3(0,0,0);
    r0.w = t_pixel_selection_object_id_texture.Load(r2.xyz).x;
  }
  o11.x = g_pixel_selection_active ? r0.w : -1;
  o1.xyzw = r1.xyzw;
  o2.w = v1.y;
  o2.xyz = r0.xyz;
  o3.w = v9.y;
  o6.xy = v5.wx;
  o6.zw = v9.zw;
  o5.xy = v1.xy;
  return;
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : TEXCOORD1,
  uint v10 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
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
  uint4 o11 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
