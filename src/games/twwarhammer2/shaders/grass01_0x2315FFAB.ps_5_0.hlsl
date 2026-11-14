// rigidgrass(wet) ps40_gbuffer_no_clip_grass (2315FFAB, 139 draws in ATN's traces): the blade's normal goes to the
// ground's with distance, the ground's at 100 m x Grass Height Distance (SI.grass_height_distance, 1: the game's).
// Decompiled from the game's 2315FFAB by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_grass.py);
// WARP tools/tw/warp_tests/par_w2/run_grass.sh.
#include "../shared.h"

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
  float g_overlay_parchment_lerp : packoffset(c47.y);
}

cbuffer ps_height_map_cb : register(b1)
{
  float4 g_mid_distance_details : packoffset(c0);
  float g_mid_distance_normal_strength : packoffset(c1);
  float2 g_mid_distance_slope : packoffset(c1.y);
  float3 g_normal_scales : packoffset(c2);
  float g_far_tile_distance : packoffset(c2.w);
  float2 g_far_blend_range : packoffset(c3);
  float4 g_far_world_write_uv_offset : packoffset(c4);
  float g_far_world_read_uv_offset : packoffset(c5);
  float4 g_far_world_uvs : packoffset(c6);
  float4x4 g_grid_transform : packoffset(c7);
  float4 g_grid_colour0 : packoffset(c11);
  float4 g_grid_colour1 : packoffset(c12);
  float2 g_xz_offset : packoffset(c13);
  float g_create_skirt : packoffset(c13.z);
  float4 g_tile_aabb : packoffset(c14);
}

cbuffer grass_PS : register(b2)
{
  float g_inv_lod_distance : packoffset(c0) = {0.00999999978};
  float2 g_alpha : packoffset(c0.y);
  float2 g_normal_scale_bias : packoffset(c1);
  float g_far_addition : packoffset(c1.z);
  float2 g_far_scale : packoffset(c2);
  float g_clip_bias : packoffset(c2.z);
}

SamplerState s_diffuse_map_s : register(s0);
SamplerState s_fire_map_s : register(s1);
SamplerState s_mid_distance_detail_map_normal_s : register(s2);
SamplerState s_mid_distance_detail_map_colour_s : register(s3);
SamplerState s_far_diffuse_map_s : register(s4);
SamplerState burn_sampler_s : register(s5);
Texture2D<float4> t_diffuse_map : register(t0);
Texture2D<float4> t_fire_map : register(t1);
Texture2D<float4> t_mid_distance_detail_map_normal : register(t2);
Texture2D<float4> t_mid_distance_detail_map_colour : register(t3);
Texture2D<float4> t_far_diffuse_map : register(t4);
Texture2D<float4> burn_texture : register(t5);
Texture2D<float4> gbuffer_normal_copy_texture : register(t6);


// 3Dmigoto declarations
#define cmp -


[earlydepthstencil]
void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 w3 : TEXCOORD6,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1,
  out float4 o2 : SV_Target2,
  out float4 o3 : SV_Target3,
  out uint o4 : SV_Target4)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t_diffuse_map.Sample(s_diffuse_map_s, v3.xy).xyzw;
  r1.x = -0.5 + r0.w;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.x = g_mid_distance_slope.y + -g_mid_distance_slope.x;
  r1.x = 1 / r1.x;
  r1.y = -g_mid_distance_slope.x + v4.y;
  r1.x = saturate(r1.y * r1.x);
  r1.y = r1.x * -2 + 3;
  r1.x = r1.x * r1.x;
  r1.x = -r1.y * r1.x + 1;
  r1.yzw = -camera_position.xyz + v2.xyz;
  r1.y = dot(r1.yzw, r1.yzw);
  r1.y = sqrt(r1.y);
  r1.z = saturate(r1.y / g_mid_distance_details.x);
  r1.y = saturate(GRASS_INV_DISTANCE(g_inv_lod_distance) * r1.y);  // RenoDX: Grass Height Distance
  r1.y = 1 + -r1.y;
  r1.x = r1.z * r1.x;
  r1.z = g_mid_distance_details.w * r1.z;
  r1.w = 1 / g_mid_distance_details.y;
  r2.xy = v2.xz * r1.ww;
  r2.zw = t_mid_distance_detail_map_normal.Sample(s_mid_distance_detail_map_normal_s, r2.xy).yw;
  r3.xyz = t_mid_distance_detail_map_colour.Sample(s_mid_distance_detail_map_colour_s, r2.xy).xyz;
  r2.xy = float2(0.00196078443,0.00196078443) + r2.zw;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r2.xy = g_mid_distance_normal_strength * r2.xy;
  r2.z = 0;
  r2.xy = mad(r1.xx, r2.xy, v4.zx);  // the game's mad (r2.z is 0)
  r2.z = v4.y;
  r4.xy = (int2)v0.xy;
  r4.zw = float2(0,0);
  r1.xw = gbuffer_normal_copy_texture.Load(r4.xyz).xy;
  r1.xw = r1.xw * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
  sincos(r1.xw, r4.xy, r5.xy);
  r6.y = r5.y;
  r6.x = r4.y;
  r5.xy = r6.xy * r5.xx;
  r5.z = r4.x;
  r2.xyz = -r5.xyz + r2.xyz;
  r1.x = saturate(v1.w * g_alpha.x + g_alpha.y);
  r4.w = r1.x * r1.y;
  r1.xyw = r4.www * r2.xyz + r5.xyz;
  r2.x = dot(r1.xyw, r1.xyw);
  r2.x = rsqrt(r2.x);
  r1.xyw = r2.xxx * r1.xyw;
  r2.x = max(abs(r1.x), abs(r1.y));
  r2.x = 1 / r2.x;
  r2.y = min(abs(r1.x), abs(r1.y));
  r2.x = r2.y * r2.x;
  r2.y = r2.x * r2.x;
  r2.z = r2.y * 0.0208350997 + -0.0851330012;
  r2.z = r2.y * r2.z + 0.180141002;
  r2.z = r2.y * r2.z + -0.330299497;
  r2.y = r2.y * r2.z + 0.999866009;
  r2.z = r2.x * r2.y;
  r2.z = r2.z * -2 + 1.57079637;
  r2.w = cmp(abs(r1.y) < abs(r1.x));
  r2.z = r2.w ? r2.z : 0;
  r2.x = r2.x * r2.y + r2.z;
  r2.yz = cmp(r1.yw < -r1.yw);
  r2.y = r2.y ? asfloat(0xc0490fdb) : 0;  // -pi (the decompile printed -3.141593)
  r2.x = r2.x + r2.y;
  r2.y = min(r1.x, r1.y);
  r2.y = cmp(r2.y < -r2.y);
  r1.x = max(r1.x, r1.y);
  r1.x = cmp(r1.x >= -r1.x);
  r1.x = r1.x ? r2.y : 0;
  r1.x = r1.x ? -r2.x : r2.x;
  r1.x = 3.14159274 + r1.x;
  r4.y = 0.159154937 * r1.x;
  r1.x = abs(r1.w) * -0.0187292993 + 0.0742610022;
  r1.x = r1.x * abs(r1.w) + -0.212114394;
  r1.x = r1.x * abs(r1.w) + 1.57072878;
  r1.y = 1 + -abs(r1.w);
  r1.y = sqrt(r1.y);
  r1.w = r1.x * r1.y;
  r1.w = r1.w * -2 + 3.14159274;
  r1.w = r2.z ? r1.w : 0;
  r1.x = r1.x * r1.y + r1.w;
  r1.x = 3.14159274 + -r1.x;
  r4.x = 0.318309873 * r1.x;
  r4.z = 0;
  o0.xyzw = saturate(r4.xyzw);
  o1.w = saturate(r4.w);
  r1.xyw = burn_texture.Sample(burn_sampler_s, v3.xy).xyz;
  r1.xyw = r1.xyw + -r0.xyz;
  r2.x = saturate(w3.y);
  r0.xyz = r2.xxx * r1.xyw + r0.xyz;
  r1.xyw = r0.xyz / r0.www;
  r0.w = cmp(0 < r0.w);
  r0.xyz = r0.www ? r1.xyw : r0.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.w = saturate(-r0.w * 15 + 1);
  r1.xy = float2(0.600000024,0.600000024) * v3.xy;
  r2.yz = t_fire_map.Sample(s_fire_map_s, r1.xy).zw;
  r1.w = log2(r2.y);
  r1.w = 1.79999995 * r1.w;
  r1.w = exp2(r1.w);
  r1.w = 10 * r1.w;
  r1.w = min(1, r1.w);
  r0.w = r1.w + r0.w;
  r0.w = 0.5 * r0.w;
  r1.y = time_in_sec * 0.0500000007 + r1.y;
  r1.x = t_fire_map.Sample(s_fire_map_s, r1.xy).x;
  r1.x = saturate(5 * r1.x);
  r1.xyw = r1.xxx * float3(0.530000031,0.0700000077,-0.120000005) + float3(0.200000003,0.200000003,0.200000003);
  r2.w = 0.25 * r2.y;
  r1.xyw = r1.xyw * float3(2,2,2) + -r2.www;
  r1.xyw = r0.www * r1.xyw + r2.www;
  r1.xyw = r1.xyw + -r0.xyz;
  r0.w = 1 + -r2.z;
  r0.w = log2(r0.w);
  r0.w = r2.x * r0.w;
  r0.w = exp2(r0.w);
  r0.w = min(1, r0.w);
  r0.w = r0.w * r2.x;
  r2.x = -0.0250000004 + r2.x;
  r2.x = max(0, r2.x);
  r0.xyz = r0.www * r1.xyw + r0.xyz;
  r1.xyw = r2.yyy * float3(0.25,0.25,0.25) + -r0.xyz;
  r0.xyz = r2.xxx * r1.xyw + r0.xyz;
  r1.xyw = float3(1,1,1) + -r0.xyz;
  r1.xyw = r1.xyw + r1.xyw;
  r2.xyz = float3(1,1,1) + -r3.xyz;
  r3.xyz = r3.xyz * r0.xyz;
  r3.xyz = r3.xyz + r3.xyz;
  r1.xyw = -r1.xyw * r2.xyz + float3(1,1,1);
  r2.xyz = cmp(r0.xyz < float3(0.5,0.5,0.5));
  r4.xyz = r2.xyz ? float3(0,0,0) : float3(1,1,1);
  r2.xyz = r2.xyz ? float3(1,1,1) : 0;
  r1.xyw = r4.xyz * r1.xyw;
  r1.xyw = r2.xyz * r3.xyz + r1.xyw;
  r1.xyw = r1.xyw + -r0.xyz;
  r0.xyz = r1.zzz * r1.xyw + r0.xyz;
  r1.xy = v2.xz / g_far_scale.xy;
  r1.xy = r1.xy * float2(1,-1) + float2(0.5,0.5);
  r1.xyz = t_far_diffuse_map.Sample(s_far_diffuse_map_s, r1.xy).xyz;
  r1.xyz = r1.xyz * r1.xyz + -r0.xyz;
  r0.w = g_far_addition + -v1.w;
  r0.w = saturate(1 + r0.w);
  o1.xyz = saturate(r0.www * r1.xyz + r0.xyz);
  o2.xyzw = float4(0,0,0,0.564705908);
  o4.x = 0;
  return;
}
