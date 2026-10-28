// Attila history in-draw motion (anti_aliasing.hpp): the game's 0xE0671FA4 with its outputs as the game's (its own draw: object_motion.hpp, its pixel shader patched by
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

cbuffer ocean_v2_tile : register(b1)
{
  float4 g_grid_dimensions : packoffset(c0);
  float2 g_meters_per_grid_sqr : packoffset(c1);
  float2 g_meters_per_tile : packoffset(c1.z);
  float4 g_field_uv_center_meters : packoffset(c2);
  float g_num_tiles_dimension : packoffset(c3);
}

cbuffer ocean_v2_water : register(b2)
{
  float g_water_level : packoffset(c0);
  float g_detail_map_repetition : packoffset(c0.y);
  float2 g_details_fade_distance : packoffset(c0.z);
  float3 g_deep_normals_sharpness : packoffset(c1);
  float2 g_foam_texuv_repetition : packoffset(c2);
  float3 g_deep_waves_scale : packoffset(c3);
  float g_height_scale_meters : packoffset(c3.w);
  float2 g_shallow_water_delta : packoffset(c4);
  float3 g_shallow_normals_sharp : packoffset(c5);
  float3 g_shallow_waves_scale : packoffset(c6);
  float g_foam_hdr_scale : packoffset(c6.w);
  float4 g_foam_scroll_time : packoffset(c7);
  float3 g_turbid_color : packoffset(c8);
  float3 g_turbid_color_bottom : packoffset(c9);
  float g_turbid_hdr_scale : packoffset(c9.w);
  float3 g_detail_normals_sharpness : packoffset(c10);
  float2 g_shore_blend_zone : packoffset(c11);
}

SamplerState s_deep_displace_sampler_s : register(s0);
SamplerState s_deep_grad_sampler_s : register(s1);
SamplerState s_shallow_displace_sampler_s : register(s2);
SamplerState s_shallow_grad_sampler_s : register(s3);
SamplerState s_land_heights_sampler_s : register(s4);
Texture2D<float4> s_deep_grad_sampler : register(t0);
Texture2D<float4> s_deep_displace_sampler : register(t1);
Texture2D<float4> s_land_heights_sampler : register(t2);
Texture2D<float4> s_shallow_displace_sampler : register(t3);
Texture2D<float4> s_shallow_grad_sampler : register(t4);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// Last frame's displacement textures (anti_aliasing.hpp).
Texture2D<float4> motion_previous_s_deep_displace_sampler : register(t17);
Texture2D<float4> motion_previous_s_shallow_displace_sampler : register(t19);

void GameMainPrevious(
  float3 v0,
  float2 v1,
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
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_meters_per_tile.xy * v1.xy;
  r0.xy = v0.xz * g_meters_per_grid_sqr.xy + r0.xy;
  r0.zw = -g_field_uv_center_meters.xy + r0.xy;
  r1.xy = g_field_uv_center_meters.zw * r0.zw;
  r1.z = -r1.y;
  r2.xyzw = s_land_heights_sampler.SampleLevel(s_land_heights_sampler_s, r1.xz, 0).xyzw;
  r0.zw = g_water_level + -r2.zx;
  r0.zw = saturate(r0.zw / g_shallow_water_delta.xy);
  r0.z = r0.z * r0.z;
  o8.w = r0.w;
  r1.yw = float2(-1,-1) + g_grid_dimensions.xy;
  r1.yw = float2(1,1) / r1.yw;
  r1.yw = abs(v0.xz) * r1.yw;
  r2.xyzw = motion_previous_s_deep_displace_sampler.SampleLevel(s_deep_displace_sampler_s, r1.yw, 0).xyzw;
  r3.xyzw = motion_previous_s_shallow_displace_sampler.SampleLevel(s_shallow_displace_sampler_s, r1.yw, 0).xyzw;
  r0.w = g_shallow_waves_scale.y * r3.y;
  r2.y = r2.y * g_deep_waves_scale.y + -r0.w;
  r2.xz = -r3.xz + r2.xz;
  r2.xz = r0.zz * r2.xz + r3.xz;
  r0.w = r0.z * r2.y + r0.w;
  r3.y = g_water_level * g_height_scale_meters + r0.w;
  r3.xz = r2.xz + r0.xy;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o1.xyz = r3.xyz;
  r0.xyw = -camera_position.xyz + r3.xyz;
  o2.zw = float2(0,0);
  o2.xy = g_detail_map_repetition * r1.yw;
  r3.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.yw, 0).xzyw;
  r3.y = 2;
  r4.xyz = g_deep_normals_sharpness.xyz * r3.xyz;
  r2.y = dot(r4.xyz, r4.xyz);
  r2.y = rsqrt(r2.y);
  r5.xyzw = s_shallow_grad_sampler.SampleLevel(s_shallow_grad_sampler_s, r1.yw, 0).xzyw;
  o5.xy = r1.yw;
  r5.y = 2;
  r6.xyz = g_shallow_normals_sharp.xyz * r5.xyz;
  r1.y = dot(r6.xyz, r6.xyz);
  r1.y = rsqrt(r1.y);
  r6.xyz = r6.xyz * r1.yyy;
  r4.xyz = r4.xyz * r2.yyy + -r6.xyz;
  o3.xyz = r0.zzz * r4.xyz + r6.xyz;
  r1.yw = -r5.xz + r3.xz;
  o4.xy = r0.zz * r1.yw + r5.xz;
  o8.z = r0.z;
  o4.zw = float2(0,0);
  r1.yw = g_num_tiles_dimension * g_meters_per_tile.xy;
  r2.xy = r2.xz / r1.yw;
  r2.z = -r2.y;
  o5.zw = r2.xz + r1.xz;
  o6.xyzw = g_foam_texuv_repetition.xxyy * v0.xzxz;
  r0.z = dot(r0.xyw, r0.xyw);
  r0.z = sqrt(r0.z);
  o7.xyz = r0.xyw / r0.zzz;
  o7.w = r0.z;
  r0.x = saturate(r0.z / g_details_fade_distance.x);
  r0.x = log2(r0.x);
  r0.x = g_details_fade_distance.y * r0.x;
  o8.x = exp2(r0.x);
  o8.y = 0;
  return;
}

void GameMain(
  float3 v0,
  float2 v1,
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
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_meters_per_tile.xy * v1.xy;
  r0.xy = v0.xz * g_meters_per_grid_sqr.xy + r0.xy;
  r0.zw = -g_field_uv_center_meters.xy + r0.xy;
  r1.xy = g_field_uv_center_meters.zw * r0.zw;
  r1.z = -r1.y;
  r2.xyzw = s_land_heights_sampler.SampleLevel(s_land_heights_sampler_s, r1.xz, 0).xyzw;
  r0.zw = g_water_level + -r2.zx;
  r0.zw = saturate(r0.zw / g_shallow_water_delta.xy);
  r0.z = r0.z * r0.z;
  o8.w = r0.w;
  r1.yw = float2(-1,-1) + g_grid_dimensions.xy;
  r1.yw = float2(1,1) / r1.yw;
  r1.yw = abs(v0.xz) * r1.yw;
  r2.xyzw = s_deep_displace_sampler.SampleLevel(s_deep_displace_sampler_s, r1.yw, 0).xyzw;
  r3.xyzw = s_shallow_displace_sampler.SampleLevel(s_shallow_displace_sampler_s, r1.yw, 0).xyzw;
  r0.w = g_shallow_waves_scale.y * r3.y;
  r2.y = r2.y * g_deep_waves_scale.y + -r0.w;
  r2.xz = -r3.xz + r2.xz;
  r2.xz = r0.zz * r2.xz + r3.xz;
  r0.w = r0.z * r2.y + r0.w;
  r3.y = g_water_level * g_height_scale_meters + r0.w;
  r3.xz = r2.xz + r0.xy;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o1.xyz = r3.xyz;
  r0.xyw = -camera_position.xyz + r3.xyz;
  o2.zw = float2(0,0);
  o2.xy = g_detail_map_repetition * r1.yw;
  r3.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.yw, 0).xzyw;
  r3.y = 2;
  r4.xyz = g_deep_normals_sharpness.xyz * r3.xyz;
  r2.y = dot(r4.xyz, r4.xyz);
  r2.y = rsqrt(r2.y);
  r5.xyzw = s_shallow_grad_sampler.SampleLevel(s_shallow_grad_sampler_s, r1.yw, 0).xzyw;
  o5.xy = r1.yw;
  r5.y = 2;
  r6.xyz = g_shallow_normals_sharp.xyz * r5.xyz;
  r1.y = dot(r6.xyz, r6.xyz);
  r1.y = rsqrt(r1.y);
  r6.xyz = r6.xyz * r1.yyy;
  r4.xyz = r4.xyz * r2.yyy + -r6.xyz;
  o3.xyz = r0.zzz * r4.xyz + r6.xyz;
  r1.yw = -r5.xz + r3.xz;
  o4.xy = r0.zz * r1.yw + r5.xz;
  o8.z = r0.z;
  o4.zw = float2(0,0);
  r1.yw = g_num_tiles_dimension * g_meters_per_tile.xy;
  r2.xy = r2.xz / r1.yw;
  r2.z = -r2.y;
  o5.zw = r2.xz + r1.xz;
  o6.xyzw = g_foam_texuv_repetition.xxyy * v0.xzxz;
  r0.z = dot(r0.xyw, r0.xyw);
  r0.z = sqrt(r0.z);
  o7.xyz = r0.xyw / r0.zzz;
  o7.w = r0.z;
  r0.x = saturate(r0.z / g_details_fade_distance.x);
  r0.x = log2(r0.x);
  r0.x = g_details_fade_distance.y * r0.x;
  o8.x = exp2(r0.x);
  o8.y = 0;
  return;
}

void main(
  float3 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 draw_o0 : SV_Position0,
  out float4 draw_o1 : TEXCOORD1,
  out float4 draw_o2 : TEXCOORD2,
  out float4 draw_o3 : TEXCOORD3,
  out float4 draw_o4 : TEXCOORD4,
  out float4 draw_o5 : TEXCOORD5,
  out float4 draw_o6 : TEXCOORD6,
  out float4 draw_o7 : TEXCOORD7,
  out float4 draw_o8 : TEXCOORD8,
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
  // This frame's: the game's outputs, with the game's rows (their jitter in them).
  motion_rows[0] = view_projection._m00_m10_m20_m30;
  motion_rows[1] = view_projection._m01_m11_m21_m31;
  motion_rows[2] = view_projection._m02_m12_m22_m32;
  motion_rows[3] = view_projection._m03_m13_m23_m33;
  motion_time = time_in_sec;
  GameMain(v0, v1, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  draw_o0 = o0.xyzw;
  draw_o1 = o1.xyzw;
  draw_o2 = o2.xyzw;
  draw_o3 = o3.xyzw;
  draw_o4 = o4.xyzw;
  draw_o5 = o5.xyzw;
  draw_o6 = o6.xyzw;
  draw_o7 = o7.xyzw;
  draw_o8 = o8.xyzw;
  const float4 game_position = o0;
  // Last frame's: world positions (identity rows).
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  // Last frame's displacement.
  GameMainPrevious(v0, v1, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  const float4 previous_world = o0;
  // Both clip positions with x and y in render pixels; this frame's without the jitter (x w).
  const float2 motion_scale = float2(0.5f, -0.5f) * motion_times.zw;
  motion_now = float4((game_position.xy - motion_game_jitter.xy * game_position.w) * motion_scale, game_position.zw);
  const float4 previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                      dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
  motion_before = float4(previous_clip.xy * motion_scale, previous_clip.zw);
}
