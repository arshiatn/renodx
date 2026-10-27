// Attila history motion (anti_aliasing.hpp): the game's 0x96708823 with its waves last frame (last frame's displacement textures); outputs SV_Position as the
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

cbuffer ocean_v2_coast : register(b3)
{
  float2 g_coast_elevate_to_shore : packoffset(c0);
  float3 g_coast_waves_scale : packoffset(c1);
  float g_coast_grad_sharp : packoffset(c1.w);
  float g_coast_teximg_border : packoffset(c2);
  float g_coast_teximg_inner : packoffset(c2.y);
  float2 g_coast_foam_hdr_scale : packoffset(c2.z);
  float3 g_coast_turbid_color : packoffset(c3);
  float3 g_coast_turbid_color_bottom : packoffset(c4);
  float g_coast_turbid_hdr_scale : packoffset(c4.w);
}

SamplerState s_deep_displace_sampler_s : register(s0);
SamplerState s_deep_grad_sampler_s : register(s1);
SamplerState s_shallow_displace_sampler_s : register(s2);
SamplerState s_shallow_grad_sampler_s : register(s3);
SamplerState s_coast_waves_displace_sampler_s : register(s4);
SamplerState s_coast_waves_grad_sampler_s : register(s5);
SamplerState s_coast_distance_sampler_s : register(s6);
SamplerState s_land_heights_sampler_s : register(s7);
Texture2D<float4> s_deep_grad_sampler : register(t0);
Texture2D<float4> s_deep_displace_sampler : register(t1);
Texture2D<float4> s_land_heights_sampler : register(t2);
Texture2D<float4> s_shallow_displace_sampler : register(t3);
Texture2D<float4> s_shallow_grad_sampler : register(t4);
Texture2D<float4> s_coast_waves_displace_sampler : register(t5);
Texture2D<float4> s_coast_waves_grad_sampler : register(t6);
Texture2D<float4> s_coast_distance_sampler : register(t7);


// 3Dmigoto declarations
#define cmp -


#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// Last frame's displacement textures (anti_aliasing.hpp).
Texture2D<float4> motion_previous_s_deep_displace_sampler : register(t17);
Texture2D<float4> motion_previous_s_shallow_displace_sampler : register(t19);
Texture2D<float4> motion_previous_s_coast_waves_displace_sampler : register(t21);

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
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = g_coast_teximg_border * 2 + g_coast_teximg_inner;
  r0.y = 1 / r0.x;
  r0.y = g_coast_teximg_border * r0.y;
  r0.zw = trunc(abs(v0.xz));
  r1.xy = float2(-1,-1) + g_grid_dimensions.xy;
  r1.zw = float2(1,1) / r1.xy;
  r1.xy = r1.xy / r0.xx;
  r0.xz = r1.zw * r0.zw;
  r1.zw = abs(v0.xz) * r1.zw;
  r0.xy = r0.xz * r1.xy + r0.yy;
  r2.xyzw = s_coast_distance_sampler.SampleLevel(s_coast_distance_sampler_s, r0.xy, 0).xyzw;
  r0.z = -g_water_level + r2.y;
  r0.z = max(0, r0.z);
  r0.z = min(g_coast_elevate_to_shore.x, r0.z);
  r0.z = saturate(r0.z / g_coast_elevate_to_shore.x);
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r3.xyzw = motion_previous_s_coast_waves_displace_sampler.SampleLevel(s_coast_waves_displace_sampler_s, r0.xy, 0).xyzw;
  r4.xyzw = motion_previous_s_deep_displace_sampler.SampleLevel(s_deep_displace_sampler_s, r1.zw, 0).xyzw;
  r5.xyzw = motion_previous_s_shallow_displace_sampler.SampleLevel(s_shallow_displace_sampler_s, r1.zw, 0).xyzw;
  r1.xy = -r5.xz + r4.xz;
  r2.zw = g_meters_per_tile.xy * v1.xy;
  r2.zw = v0.xz * g_meters_per_grid_sqr.xy + r2.zw;
  r4.xz = -g_field_uv_center_meters.xy + r2.zw;
  r6.xy = g_field_uv_center_meters.zw * r4.xz;
  r6.z = -r6.y;
  r7.xyzw = s_land_heights_sampler.SampleLevel(s_land_heights_sampler_s, r6.xz, 0).xyzw;
  r4.xz = g_water_level + -r7.zx;
  r4.xz = saturate(r4.xz / g_shallow_water_delta.xy);
  r0.w = r4.x * r4.x;
  o8.w = r4.z;
  r1.xy = r0.ww * r1.xy + r5.xz;
  r3.w = g_shallow_waves_scale.y * r5.y;
  r5.xz = r2.zw + r1.xy;
  r1.x = r4.y * g_deep_waves_scale.y + -r3.w;
  r1.x = r0.w * r1.x + r3.w;
  r5.y = g_water_level * g_height_scale_meters + r1.x;
  r3.xyz = r3.xyz * g_coast_waves_scale.xyz + r5.xyz;
  r1.x = g_water_level * g_height_scale_meters + -r3.y;
  r1.x = r0.z * r1.x + r3.y;
  r3.y = r0.z * g_coast_elevate_to_shore.y + r1.x;
  r1.xy = r3.xz + -r2.zw;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o1.xyz = r3.xyz;
  r3.xyz = -camera_position.xyz + r3.xyz;
  o2.zw = r0.xy;
  r4.xyzw = s_coast_waves_grad_sampler.SampleLevel(s_coast_waves_grad_sampler_s, r0.xy, 0).xyzw;
  o2.xy = g_detail_map_repetition * r1.zw;
  r0.xy = g_coast_grad_sharp * r4.xy;
  r5.xyzw = s_shallow_grad_sampler.SampleLevel(s_shallow_grad_sampler_s, r1.zw, 0).xyzw;
  r2.zw = -r4.xy * g_coast_grad_sharp + r5.xy;
  o8.y = r4.z;
  r0.xz = r2.xx * r2.zw + r0.xy;
  o4.zw = r2.xy;
  r0.y = 2;
  r2.xyz = g_shallow_normals_sharp.xyz * r0.xyz;
  r0.y = dot(r2.xyz, r2.xyz);
  r0.y = rsqrt(r0.y);
  r2.xyz = r2.xyz * r0.yyy;
  r4.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.zw, 0).xyzw;
  o5.xy = r1.zw;
  r5.xz = r4.xy;
  r1.zw = r4.xy + -r0.xz;
  o4.xy = r0.ww * r1.zw + r0.xz;
  r5.y = 2;
  r0.xyz = g_deep_normals_sharpness.xyz * r5.xyz;
  r1.z = dot(r0.xyz, r0.xyz);
  r1.z = rsqrt(r1.z);
  r0.xyz = r0.xyz * r1.zzz + -r2.xyz;
  o3.xyz = r0.www * r0.xyz + r2.xyz;
  o8.z = r0.w;
  r0.xy = g_num_tiles_dimension * g_meters_per_tile.xy;
  r0.xy = r1.xy / r0.xy;
  r0.z = -r0.y;
  o5.zw = r6.xz + r0.xz;
  o6.xyzw = g_foam_texuv_repetition.xxyy * v0.xzxz;
  r0.x = dot(r3.xyz, r3.xyz);
  r0.x = sqrt(r0.x);
  o7.xyz = r3.xyz / r0.xxx;
  o7.w = r0.x;
  r0.x = saturate(r0.x / g_details_fade_distance.x);
  r0.x = log2(r0.x);
  r0.x = g_details_fade_distance.y * r0.x;
  o8.x = exp2(r0.x);
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = g_coast_teximg_border * 2 + g_coast_teximg_inner;
  r0.y = 1 / r0.x;
  r0.y = g_coast_teximg_border * r0.y;
  r0.zw = trunc(abs(v0.xz));
  r1.xy = float2(-1,-1) + g_grid_dimensions.xy;
  r1.zw = float2(1,1) / r1.xy;
  r1.xy = r1.xy / r0.xx;
  r0.xz = r1.zw * r0.zw;
  r1.zw = abs(v0.xz) * r1.zw;
  r0.xy = r0.xz * r1.xy + r0.yy;
  r2.xyzw = s_coast_distance_sampler.SampleLevel(s_coast_distance_sampler_s, r0.xy, 0).xyzw;
  r0.z = -g_water_level + r2.y;
  r0.z = max(0, r0.z);
  r0.z = min(g_coast_elevate_to_shore.x, r0.z);
  r0.z = saturate(r0.z / g_coast_elevate_to_shore.x);
  r0.w = r0.z * -2 + 3;
  r0.z = r0.z * r0.z;
  r0.z = r0.w * r0.z;
  r3.xyzw = s_coast_waves_displace_sampler.SampleLevel(s_coast_waves_displace_sampler_s, r0.xy, 0).xyzw;
  r4.xyzw = s_deep_displace_sampler.SampleLevel(s_deep_displace_sampler_s, r1.zw, 0).xyzw;
  r5.xyzw = s_shallow_displace_sampler.SampleLevel(s_shallow_displace_sampler_s, r1.zw, 0).xyzw;
  r1.xy = -r5.xz + r4.xz;
  r2.zw = g_meters_per_tile.xy * v1.xy;
  r2.zw = v0.xz * g_meters_per_grid_sqr.xy + r2.zw;
  r4.xz = -g_field_uv_center_meters.xy + r2.zw;
  r6.xy = g_field_uv_center_meters.zw * r4.xz;
  r6.z = -r6.y;
  r7.xyzw = s_land_heights_sampler.SampleLevel(s_land_heights_sampler_s, r6.xz, 0).xyzw;
  r4.xz = g_water_level + -r7.zx;
  r4.xz = saturate(r4.xz / g_shallow_water_delta.xy);
  r0.w = r4.x * r4.x;
  o8.w = r4.z;
  r1.xy = r0.ww * r1.xy + r5.xz;
  r3.w = g_shallow_waves_scale.y * r5.y;
  r5.xz = r2.zw + r1.xy;
  r1.x = r4.y * g_deep_waves_scale.y + -r3.w;
  r1.x = r0.w * r1.x + r3.w;
  r5.y = g_water_level * g_height_scale_meters + r1.x;
  r3.xyz = r3.xyz * g_coast_waves_scale.xyz + r5.xyz;
  r1.x = g_water_level * g_height_scale_meters + -r3.y;
  r1.x = r0.z * r1.x + r3.y;
  r3.y = r0.z * g_coast_elevate_to_shore.y + r1.x;
  r1.xy = r3.xz + -r2.zw;
  r3.w = 1;
  o0.x = dot(r3.xyzw, motion_rows[0]);
  o0.y = dot(r3.xyzw, motion_rows[1]);
  o0.z = dot(r3.xyzw, motion_rows[2]);
  o0.w = dot(r3.xyzw, motion_rows[3]);
  o1.xyz = r3.xyz;
  r3.xyz = -camera_position.xyz + r3.xyz;
  o2.zw = r0.xy;
  r4.xyzw = s_coast_waves_grad_sampler.SampleLevel(s_coast_waves_grad_sampler_s, r0.xy, 0).xyzw;
  o2.xy = g_detail_map_repetition * r1.zw;
  r0.xy = g_coast_grad_sharp * r4.xy;
  r5.xyzw = s_shallow_grad_sampler.SampleLevel(s_shallow_grad_sampler_s, r1.zw, 0).xyzw;
  r2.zw = -r4.xy * g_coast_grad_sharp + r5.xy;
  o8.y = r4.z;
  r0.xz = r2.xx * r2.zw + r0.xy;
  o4.zw = r2.xy;
  r0.y = 2;
  r2.xyz = g_shallow_normals_sharp.xyz * r0.xyz;
  r0.y = dot(r2.xyz, r2.xyz);
  r0.y = rsqrt(r0.y);
  r2.xyz = r2.xyz * r0.yyy;
  r4.xyzw = s_deep_grad_sampler.SampleLevel(s_deep_grad_sampler_s, r1.zw, 0).xyzw;
  o5.xy = r1.zw;
  r5.xz = r4.xy;
  r1.zw = r4.xy + -r0.xz;
  o4.xy = r0.ww * r1.zw + r0.xz;
  r5.y = 2;
  r0.xyz = g_deep_normals_sharpness.xyz * r5.xyz;
  r1.z = dot(r0.xyz, r0.xyz);
  r1.z = rsqrt(r1.z);
  r0.xyz = r0.xyz * r1.zzz + -r2.xyz;
  o3.xyz = r0.www * r0.xyz + r2.xyz;
  o8.z = r0.w;
  r0.xy = g_num_tiles_dimension * g_meters_per_tile.xy;
  r0.xy = r1.xy / r0.xy;
  r0.z = -r0.y;
  o5.zw = r6.xz + r0.xz;
  o6.xyzw = g_foam_texuv_repetition.xxyy * v0.xzxz;
  r0.x = dot(r3.xyz, r3.xyz);
  r0.x = sqrt(r0.x);
  o7.xyz = r3.xyz / r0.xxx;
  o7.w = r0.x;
  r0.x = saturate(r0.x / g_details_fade_distance.x);
  r0.x = log2(r0.x);
  r0.x = g_details_fade_distance.y * r0.x;
  o8.x = exp2(r0.x);
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
