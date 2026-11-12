// Attila world motion (anti_aliasing.hpp): the game's 0x8DE94E2F with its world position for ray tracing's stream output
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

cbuffer cb_environment : register(b2)
{
  float2 g_wind_direction : packoffset(c0);
  float2 g_wind_vector : packoffset(c0.z);
  float2 g_wind_vector_main : packoffset(c1);
  float g_wind_gust_start_time : packoffset(c1.z);
  float g_wind_vegetation_multiplier : packoffset(c1.w);
  float g_wind_vegetation_hstrength : packoffset(c2);
  float g_wind_vegetation_vstrength : packoffset(c2.y);
  float2 g_wind_vegetation_tremble : packoffset(c2.z);
  float2 g_wind_vegetation_weaviness : packoffset(c3);
  float g_wind_vegetation_trunk_movement : packoffset(c3.z);
  float g_wind_vegetation_lerp : packoffset(c3.w);
  float2 g_wind_offset : packoffset(c4);
  float g_snowing_intensity : packoffset(c4.z);
  int g_weather_type : packoffset(c4.w);
  int g_season_type : packoffset(c5);
  float g_snow_mask : packoffset(c5.y);
  float g_creep_intensity : packoffset(c5.z);
  float g_cubemap_sun_colour_scale : packoffset(c5.w);
  float g_campaign_vegetation_tint_scale : packoffset(c6);
}

cbuffer shader_params : register(b3)
{
  float sp_Flag_Base_Mask_Gamma : packoffset(c0);
  float sp_Flag_Base_Mask_Multiplier : packoffset(c0.y);
  float sp_Flag_Random_Offset : packoffset(c0.z);
  float sp_Flag_Y_Position : packoffset(c0.w);
  float sp_Wave_Attenuation_Gain : packoffset(c1);
  float sp_Wave_Attenuation_Gamma : packoffset(c1.y);
  float3 sp_Wave_Noise_B : packoffset(c2);
  float3 sp_Wave_Noise_G : packoffset(c3);
  float3 sp_Wave_Noise_R : packoffset(c4);
  float sp_Wave_Speed_Wind_Multiplier_Max : packoffset(c4.w);
  float sp_Wave_Speed_Wind_Multiplier_Min : packoffset(c5);
  float sp_Wave_Speed_X : packoffset(c5.y);
  float sp_Wave_Speed_Y : packoffset(c5.z);
  float sp_Wave_Texture_Resolution : packoffset(c5.w);
  float sp_Wave_Texture_Scale : packoffset(c6);
  float sp_Wave_Texture_Y_Sample : packoffset(c6.y);
  float sp_Wave_Y_input_multiplier : packoffset(c6.z);
  float sp_Wind_Input_Gamma : packoffset(c6.w);
  float sp_Wind_Input_Multiplier : packoffset(c7);
  float sp_Wind_Input_Offset : packoffset(c7.y);
}

Texture2D<uint> t_pixel_selection_object_id_texture : register(t0);
Texture2D<float4> t_Wave_Texture : register(t1);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  uint v13,
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
  inout float4 o12,
  inout uint4 o13,
  inout float4 p13)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r1.xyz * r0.xxx;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r1.xyz * r0.xxx;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r1.x = dot(r0.xyz, v6.xyz);
  r1.y = dot(r0.xyz, v7.xyz);
  r1.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  o5.xyz = r1.xyz * r0.xxx;
  r0.x = v3.w;
  r0.y = v4.w;
  r0.z = v2.w;
  o9.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  if (g_pixel_selection_active != 0) {
    r0.x = (int)v13.x + g_pixel_selection_start_pixel_index;
    r0.yzw = float3(0,0,0);
    r0.x = t_pixel_selection_object_id_texture.Load(r0.xyz).x;
  }
  o13.x = g_pixel_selection_active ? r0.x : -1;
  r0.x = dot(g_wind_vector_main.xy, g_wind_vector_main.xy);
  r0.x = sqrt(r0.x);
  r0.y = cmp(0.00999999978 >= r0.x);
  r0.x = r0.y ? 0.200000003 : r0.x;
  r0.x = sp_Wind_Input_Multiplier * r0.x;
  r0.x = log2(r0.x);
  r0.x = sp_Wind_Input_Gamma * r0.x;
  r0.x = exp2(r0.x);
  r0.x = saturate(sp_Wind_Input_Offset + r0.x);
  r0.z = -sp_Wave_Speed_Wind_Multiplier_Min + sp_Wave_Speed_Wind_Multiplier_Max;
  r0.z = r0.x * r0.z + sp_Wave_Speed_Wind_Multiplier_Min;
  // mul r1.xy, cb0[26].xxxx, cb3[5].yzyy: the wave speed's x and y (3Dmigoto wrote Speed_X twice).
  r1.xy = float2(sp_Wave_Speed_X, sp_Wave_Speed_Y) * motion_time;
  r0.w = frac(v6.w);
  r1.z = frac(v8.w);
  r0.w = r1.z + r0.w;
  r0.w = frac(r0.w);
  r0.w = sp_Flag_Random_Offset * r0.w;
  r0.zw = r0.zz * r1.xy + r0.ww;
  r0.zw = frac(r0.zw);
  r0.z = v0.z * sp_Wave_Texture_Scale + -r0.z;
  r0.z = frac(r0.z);
  r0.w = sp_Wave_Texture_Y_Sample * sp_Wave_Texture_Scale + -r0.w;
  r0.w = frac(r0.w);
  r0.zw = sp_Wave_Texture_Resolution * r0.zw;
  r1.xy = (int2)r0.zw;
  r1.zw = float2(0,0);
  r1.xyz = t_Wave_Texture.Load(r1.xyz).xyz;
  r1.xyz = float3(-0.5,-0.5,-0.5) + r1.xyz;
  r2.xyz = sp_Wave_Noise_G.zyx * r1.yyy;
  r1.xyw = sp_Wave_Noise_R.zyx * r1.xxx + r2.xyz;
  r1.xyz = sp_Wave_Noise_B.zyx * r1.zzz + r1.xyw;
  r0.z = 1 + -v1.y;
  r0.z = log2(r0.z);
  r0.w = sp_Wave_Attenuation_Gamma * r0.z;
  r0.w = exp2(r0.w);
  r0.w = sp_Wave_Attenuation_Gain * r0.w;
  r1.xyz = r1.xyz * r0.www + v0.zyx;
  r1.w = -sp_Flag_Y_Position + r1.y;
  r0.x = r0.x * -0.200000003 + 0.200000003;
  r0.x = 6.28000021 * r0.x;
  r0.z = sp_Flag_Base_Mask_Gamma * r0.z;
  r0.z = exp2(r0.z);
  r0.z = sp_Flag_Base_Mask_Multiplier * r0.z;
  r0.x = r0.x * r0.z;
  sincos(r0.x, r2.x, r3.x);
  r3.y = r3.x;
  r3.z = r2.x;
  r2.x = dot(r1.xw, r3.yz);
  r0.yz = r0.yy ? float2(0.100000001,0) : g_wind_vector_main.xy;
  r0.w = dot(r0.yz, r0.yz);
  r0.w = rsqrt(r0.w);
  r0.yz = r0.yz * r0.ww;
  r4.xw = v6.xw;
  r4.y = v7.x;
  r4.z = v8.x;
  r0.w = dot(r4.xyz, r4.xyz);
  r0.w = sqrt(r0.w);
  r4.xy = r0.zy * r0.ww;
  r5.x = v6.z;
  r5.y = v7.z;
  r5.z = v8.z;
  r0.w = dot(r5.xyz, r5.xyz);
  r0.w = sqrt(r0.w);
  r4.z = -r0.z * r0.w;
  r2.y = r1.z;
  r2.z = 1;
  r5.x = dot(r2.xyz, r4.yzw);
  r3.x = sin(-r0.x);
  r0.x = dot(r1.xw, r3.xy);
  r1.x = sp_Flag_Y_Position + r0.x;
  r3.x = v6.y;
  r3.y = v7.y;
  r3.z = v8.y;
  r0.x = dot(r3.xyz, r3.xyz);
  r3.x = sqrt(r0.x);
  r1.y = 1;
  r3.y = v7.w;
  r5.y = dot(r1.xy, r3.xy);
  r4.y = r0.y * r0.w;
  r4.z = v8.w;
  r5.z = dot(r2.xyz, r4.xyz);
  r5.w = 1;
  r0.x = dot(r5.xyzw, motion_rows[0]);
  r0.y = dot(r5.xyzw, motion_rows[1]);
  o0.z = dot(r5.xyzw, motion_rows[2]);
  r0.w = dot(r5.xyzw, motion_rows[3]);
  r0.z = cmp(0 != v9.z);
  if (r0.z != 0) {
    r1.xyz = camera_position.xyz + -r5.xyz;
    r0.z = dot(r1.xyz, r1.xyz);
    r0.z = sqrt(r0.z);
    r0.z = 80 + -r0.z;
    r1.x = saturate(0.0250000004 * r0.z);
  } else {
    r1.x = 0;
  }
  o0.xyw = r0.xyw;
  o1.xyzw = r5.xyzw;
  o2.xyzw = v1.xyzw;
  o3.w = g_wind_direction.x;
  o4.w = g_wind_direction.y;
  r1.yzw = v9.xyw;
  o6.xyzw = r1.yzxw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o10.xyz = r0.xyw;
  o10.w = v9.y;
  o11.xyzw = v5.xyzw;
  o12.xyzw = v12.xyzw;
  p13.x = v6.w;
  p13.y = v7.w;
  p13.z = v8.w;
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : NORMAL0,
  float4 v3 : NORMAL1,
  float4 v4 : NORMAL2,
  float4 v5 : COLOR0,
  float4 v6 : POSITION1,
  float4 v7 : POSITION2,
  float4 v8 : POSITION3,
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  float4 v12 : POSITION7,
  uint v13 : SV_InstanceID0,
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
  float4 o11 = 0;
  float4 o12 = 0;
  uint4 o13 = 0;
  float4 p13 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12, o13, p13);
  const float4 world = o0;
  motion_world = world;
  motion_position = 0.f;
}
