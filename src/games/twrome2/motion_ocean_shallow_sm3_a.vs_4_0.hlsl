// Attila history motion (anti_aliasing.hpp): the game's 0x15C3A7A7 with its waves last frame (last frame's displacement textures); outputs SV_Position as the
// game's, this frame's and last frame's clip positions without the jitter.
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
SamplerState s_land_heights_sampler_s : register(s3);
Texture2D<float4> s_deep_grad_sampler : register(t0);
Texture2D<float4> s_deep_displace_sampler : register(t1);
Texture2D<float4> s_land_heights_sampler : register(t2);
Texture2D<float4> s_shallow_displace_sampler : register(t3);


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
  float4 r0,r1,r2,r3,r4;
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
  o2.xy = g_detail_map_repetition * r1.yw;
  o2.zw = float2(0,0);
  r3.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.yw, 0).xyzw;
  o5.xy = r1.yw;
  r4.xz = r3.xy;
  o4.xy = r3.xy;
  r4.y = 2;
  r3.xyz = g_shallow_normals_sharp.xyz * r4.xyz;
  r4.xyz = g_deep_normals_sharpness.xyz * r4.xyz;
  r1.y = dot(r3.xyz, r3.xyz);
  r1.y = rsqrt(r1.y);
  r3.xyz = r3.xyz * r1.yyy;
  r1.y = dot(r4.xyz, r4.xyz);
  r1.y = rsqrt(r1.y);
  r4.xyz = r4.xyz * r1.yyy + -r3.xyz;
  o3.xyz = r0.zzz * r4.xyz + r3.xyz;
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
  float4 r0,r1,r2,r3,r4;
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
  o2.xy = g_detail_map_repetition * r1.yw;
  o2.zw = float2(0,0);
  r3.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.yw, 0).xyzw;
  o5.xy = r1.yw;
  r4.xz = r3.xy;
  o4.xy = r3.xy;
  r4.y = 2;
  r3.xyz = g_shallow_normals_sharp.xyz * r4.xyz;
  r4.xyz = g_deep_normals_sharpness.xyz * r4.xyz;
  r1.y = dot(r3.xyz, r3.xyz);
  r1.y = rsqrt(r1.y);
  r3.xyz = r3.xyz * r1.yyy;
  r1.y = dot(r4.xyz, r4.xyz);
  r1.y = rsqrt(r1.y);
  r4.xyz = r4.xyz * r1.yyy + -r3.xyz;
  o3.xyz = r0.zzz * r4.xyz + r3.xyz;
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
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  const float4 world = o0;
  // Last frame's displacement.
  GameMainPrevious(v0, v1, o0, o1, o2, o3, o4, o5, o6, o7, o8);
  const float4 previous_world = o0;
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
