// rigidgrass ps50_gbuffer_grass_ms (MSAA G-buffer): in no trace of ATN's settings; the game's, with Grass Height Distance (SI.grass_height_distance), 1: the game's.
// Decompiled from the game's EFA7F77B by 3Dmigoto v1.3.16 (tools/tw/wh2/par_w2_make_perms.py); WARP
// tools/tw/warp_tests/par_w2/run_perms.sh.
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
Texture2DMS<float> gbuffer_normal_copy_texture_ms : register(t6);


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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = g_mid_distance_slope.y + -g_mid_distance_slope.x;
  r0.x = 1 / r0.x;
  r0.y = -g_mid_distance_slope.x + v4.y;
  r0.x = saturate(r0.y * r0.x);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = -r0.y * r0.x + 1;
  r0.yzw = -camera_position.xyz + v2.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.z = saturate(r0.y / g_mid_distance_details.x);
  r0.y = saturate(GRASS_INV_DISTANCE(g_inv_lod_distance) * r0.y);  // RenoDX: Grass Height Distance
  r0.y = 1 + -r0.y;
  r0.x = r0.z * r0.x;
  r0.z = g_mid_distance_details.w * r0.z;
  r0.w = 1 / g_mid_distance_details.y;
  r1.xy = v2.xz * r0.ww;
  r1.zw = t_mid_distance_detail_map_normal.Sample(s_mid_distance_detail_map_normal_s, r1.xy).yw;
  r2.xyz = t_mid_distance_detail_map_colour.Sample(s_mid_distance_detail_map_colour_s, r1.xy).xyz;
  r1.xy = float2(0.00196078443,0.00196078443) + r1.zw;
  r1.xy = r1.xy * float2(2,2) + float2(-1,-1);
  r1.xy = g_mid_distance_normal_strength * r1.xy;
  r1.z = 0;
  r1.xy = mad(r0.xx, r1.xy, v4.zx);  // the game's mad (r1.z is 0)
  r1.z = v4.y;
  r3.xy = (int2)v0.xy;
  r3.zw = float2(0,0);
  r0.x = gbuffer_normal_copy_texture_ms.Load(r3.xy, 0).x;
  r0.xw = r0.xx * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
  sincos(r0.xw, r3.xy, r4.xy);
  r5.y = r4.y;
  r5.x = r3.y;
  r4.xy = r5.xy * r4.xx;
  r4.z = r3.x;
  r1.xyz = -r4.xyz + r1.xyz;
  r0.x = saturate(v1.w * g_alpha.x + g_alpha.y);
  r0.x = r0.x * r0.y;
  r1.xyz = r0.xxx * r1.xyz + r4.xyz;
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.y);
  r1.xyz = r1.xyz * r0.yyy;
  r0.y = max(abs(r1.x), abs(r1.y));
  r0.y = 1 / r0.y;
  r0.w = min(abs(r1.x), abs(r1.y));
  r0.y = r0.w * r0.y;
  r0.w = r0.y * r0.y;
  r1.w = r0.w * 0.0208350997 + -0.0851330012;
  r1.w = r0.w * r1.w + 0.180141002;
  r1.w = r0.w * r1.w + -0.330299497;
  r0.w = r0.w * r1.w + 0.999866009;
  r1.w = r0.y * r0.w;
  r1.w = r1.w * -2 + 1.57079637;
  r2.w = cmp(abs(r1.y) < abs(r1.x));
  r1.w = r2.w ? r1.w : 0;
  r0.y = r0.y * r0.w + r1.w;
  r3.xy = cmp(r1.yz < -r1.yz);
  r0.w = r3.x ? asfloat(0xc0490fdb) : 0;  // -pi (the decompile printed -3.141593)
  r0.y = r0.y + r0.w;
  r0.w = min(r1.x, r1.y);
  r0.w = cmp(r0.w < -r0.w);
  r1.x = max(r1.x, r1.y);
  r1.x = cmp(r1.x >= -r1.x);
  r0.w = r0.w ? r1.x : 0;
  r0.y = r0.w ? -r0.y : r0.y;
  r0.y = 3.14159274 + r0.y;
  r4.y = 0.159154937 * r0.y;
  r0.y = abs(r1.z) * -0.0187292993 + 0.0742610022;
  r0.y = r0.y * abs(r1.z) + -0.212114394;
  r0.y = r0.y * abs(r1.z) + 1.57072878;
  r0.w = 1 + -abs(r1.z);
  r0.w = sqrt(r0.w);
  r1.x = r0.y * r0.w;
  r1.x = r1.x * -2 + 3.14159274;
  r1.x = r3.y ? r1.x : 0;
  r0.y = r0.y * r0.w + r1.x;
  r0.y = 3.14159274 + -r0.y;
  r4.x = 0.318309873 * r0.y;
  r4.z = 0;
  r1.xyzw = t_diffuse_map.Sample(s_diffuse_map_s, v3.xy).xyzw;
  r4.w = r1.w * r0.x;
  o0.xyzw = saturate(r4.xyzw);
  o1.w = saturate(r4.w);
  r0.xyw = burn_texture.Sample(burn_sampler_s, v3.xy).xyz;
  r0.xyw = r0.xyw + -r1.xyz;
  r2.w = saturate(w3.y);
  r0.xyw = r2.www * r0.xyw + r1.xyz;
  r1.xyz = r0.xyw / r1.www;
  r1.w = cmp(0 < r1.w);
  r0.xyw = r1.www ? r1.xyz : r0.xyw;
  r1.x = dot(r0.xyw, float3(0.212599993,0.715200007,0.0722000003));
  r1.x = saturate(-r1.x * 15 + 1);
  r1.yz = float2(0.600000024,0.600000024) * v3.xy;
  r3.xy = t_fire_map.Sample(s_fire_map_s, r1.yz).zw;
  r1.w = log2(r3.x);
  r1.w = 1.79999995 * r1.w;
  r1.w = exp2(r1.w);
  r1.w = 10 * r1.w;
  r1.w = min(1, r1.w);
  r1.x = r1.x + r1.w;
  r1.x = 0.5 * r1.x;
  r1.z = time_in_sec * 0.0500000007 + r1.z;
  r1.y = t_fire_map.Sample(s_fire_map_s, r1.yz).x;
  r1.y = saturate(5 * r1.y);
  r1.yzw = r1.yyy * float3(0.530000031,0.0700000077,-0.120000005) + float3(0.200000003,0.200000003,0.200000003);
  r3.z = 0.25 * r3.x;
  r1.yzw = r1.yzw * float3(2,2,2) + -r3.zzz;
  r1.xyz = r1.xxx * r1.yzw + r3.zzz;
  r1.xyz = r1.xyz + -r0.xyw;
  r1.w = 1 + -r3.y;
  r1.w = log2(r1.w);
  r1.w = r2.w * r1.w;
  r1.w = exp2(r1.w);
  r1.w = min(1, r1.w);
  r1.w = r1.w * r2.w;
  r2.w = -0.0250000004 + r2.w;
  r2.w = max(0, r2.w);
  r0.xyw = r1.www * r1.xyz + r0.xyw;
  r1.xyz = r3.xxx * float3(0.25,0.25,0.25) + -r0.xyw;
  r0.xyw = r2.www * r1.xyz + r0.xyw;
  r1.xyz = float3(1,1,1) + -r0.xyw;
  r1.xyz = r1.xyz + r1.xyz;
  r3.xyz = float3(1,1,1) + -r2.xyz;
  r2.xyz = r2.xyz * r0.xyw;
  r2.xyz = r2.xyz + r2.xyz;
  r1.xyz = -r1.xyz * r3.xyz + float3(1,1,1);
  r3.xyz = cmp(r0.xyw < float3(0.5,0.5,0.5));
  r4.xyz = r3.xyz ? float3(0,0,0) : float3(1,1,1);
  r3.xyz = r3.xyz ? float3(1,1,1) : 0;
  r1.xyz = r4.xyz * r1.xyz;
  r1.xyz = r3.xyz * r2.xyz + r1.xyz;
  r1.xyz = r1.xyz + -r0.xyw;
  r0.xyz = r0.zzz * r1.xyz + r0.xyw;
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
