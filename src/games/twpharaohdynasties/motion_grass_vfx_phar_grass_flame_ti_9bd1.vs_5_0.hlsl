// Attila wind motion (anti_aliasing.hpp): the game's 0x9BD142BD with its wind at this frame's and last frame's time; outputs SV_Position as the game's, this frame's
// and last frame's clip positions without the jitter.
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
  float sp_Alpha_Gain : packoffset(c0);
  float sp_Alpha_Gamma : packoffset(c0.y);
  float sp_Alpha_Lift : packoffset(c0.z);
  float sp_Flame_Gain : packoffset(c0.w);
  float sp_Flame_Gamma : packoffset(c1);
  float sp_Flame_Lift : packoffset(c1.y);
  float sp_Flame_Mask_Gamma : packoffset(c1.z);
  float2 sp_Flame_Offset : packoffset(c2);
  float2 sp_Flame_Pan_Direction : packoffset(c2.z);
  float3 sp_Flame_Post_Color_Balance : packoffset(c3);
  float sp_Flame_Post_Gamma : packoffset(c3.w);
  float sp_Flame_Post_Multiplier : packoffset(c4);
  float sp_Flame_Scale_AI_Input : packoffset(c4.y);
  float2 sp_Flame_Tile : packoffset(c4.z);
  float sp_Flames_Base_Speed : packoffset(c5);
  float sp_LUT_V_Selector : packoffset(c5.y);
  float sp_Noise_Multiplier_X : packoffset(c5.z);
  float sp_Noise_Multiplier_Y : packoffset(c5.w);
  float sp_Noise_Multiplier_Z : packoffset(c6);
  float sp_Noise_Ramp_Gamma : packoffset(c6.y);
  float sp_Noise_Ramp_Multiplier : packoffset(c6.z);
  float sp_Noise_Speed_U : packoffset(c6.w);
  float sp_Noise_Speed_V : packoffset(c7);
  float sp_Noise_Tile : packoffset(c7.y);
}

Texture2D<uint> t_pixel_selection_object_id_texture : register(t0);
Texture2D<float4> t_Noise_Texture : register(t1);


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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = v0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, v6.xyzw);
  r1.w = dot(r0.xyzw, v7.xyzw);
  r1.z = dot(r0.xyzw, v8.xyzw);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o3.xyz = r2.xyz * r0.xxx;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o4.xyz = r2.xyz * r0.xxx;
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r2.x = dot(r0.xyz, v6.xyz);
  r2.y = dot(r0.xyz, v7.xyz);
  r2.z = dot(r0.xyz, v8.xyz);
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  o5.xyz = r2.xyz * r0.xxx;
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
  r0.x = -v7.w + r1.w;
  r0.y = floor(v9.w);
  r0.y = 0.00100000005 * r0.y;
  r1.y = r0.x * r0.y + v7.w;
  r0.xz = sp_Noise_Tile * v1.zw;
  r0.x = sp_Noise_Speed_U * motion_time + r0.x;
  r0.w = cmp(r0.x >= -r0.x);
  r0.x = frac(abs(r0.x));
  r0.x = r0.w ? r0.x : -r0.x;
  r0.z = motion_time * sp_Noise_Speed_V + r0.z;
  r0.w = cmp(r0.z >= -r0.z);
  r0.z = frac(abs(r0.z));
  r0.z = r0.w ? r0.z : -r0.z;
  r0.xz = float2(512,512) * r0.xz;
  r2.xy = (int2)r0.xz;
  r2.zw = float2(0,0);
  r0.x = t_Noise_Texture.Load(r2.xyz).x;
  r0.x = -0.5 + r0.x;
  r2.xy = sp_Noise_Multiplier_X * r0.xx;
  r2.z = sp_Noise_Multiplier_Z * r0.x;
  r0.x = 1 + -v1.w;
  r0.x = log2(r0.x);
  r0.x = sp_Noise_Ramp_Gamma * r0.x;
  r0.x = exp2(r0.x);
  r0.y = r0.y * r0.y;
  r0.y = sp_Noise_Ramp_Multiplier * r0.y;
  r0.x = r0.x * r0.y;
  r0.yzw = r2.xyz * r0.xxx + r1.xyz;
  r1.xy = g_wind_vector.xy * r0.xx;
  r1.xz = float2(3,3) * r1.xy;
  r1.y = 0;
  r0.xyz = r1.xyz + r0.yzw;
  r0.w = 1;
  r1.x = dot(r0.xyzw, motion_rows[0]);
  r1.y = dot(r0.xyzw, motion_rows[1]);
  o0.z = dot(r0.xyzw, motion_rows[2]);
  r1.w = dot(r0.xyzw, motion_rows[3]);
  r1.z = cmp(0 != v9.z);
  if (r1.z != 0) {
    r2.xyz = camera_position.xyz + -r0.xyz;
    r1.z = dot(r2.xyz, r2.xyz);
    r1.z = sqrt(r1.z);
    r1.z = 80 + -r1.z;
    r2.x = saturate(0.0250000004 * r1.z);
  } else {
    r2.x = 0;
  }
  o0.xyw = r1.xyw;
  o1.xyzw = r0.xyzw;
  o2.xyzw = v1.xyzw;
  o3.w = g_wind_direction.x;
  o4.w = g_wind_direction.y;
  r2.yzw = v9.xyw;
  o6.xyzw = r2.yzxw;
  o7.xyzw = v10.xyzw;
  o8.xyzw = v11.xyzw;
  o10.xyz = r1.xyw;
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
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1)
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
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12, o13, p13);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
