/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III ray tracing (twattila/rt_scene.hpp): fx_rigidbranch_v3's branches (474F97FB) in world space for the stream output, a placed mesh's first instance
// or its swaying copy. The game's code; made by tools\tw\wh3_batch3\b8_trees.py.
#if defined(RT_CAPTURE_BRANCH_UV)
#include "../twattila/rt_capture.hlsli"
#endif

struct TREE_BRANCH_SIM_INSTANCE_DATA
{
    float wind_intensity;          // Offset:    0
    float trunk_amplitude_mul;     // Offset:    4
    float trunk_phase;             // Offset:    8
    float leaf_strength_mul;       // Offset:   12
    float world_position[3];       // Offset:   16
    float orientation_world[4];    // Offset:   28
    uint tree_data_id;             // Offset:   44
    uint index;                    // Offset:   48
};

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
  float4 g_camera_temp0 : packoffset(c42);
  float4 g_camera_temp1 : packoffset(c43);
  float4 g_camera_temp2 : packoffset(c44);
  float4 g_clip_rect : packoffset(c45);
  float3 g_vr_head_rotation : packoffset(c46);
  int g_num_of_samples : packoffset(c46.w);
  float g_supersampling : packoffset(c47);
  float4 g_mouse_position : packoffset(c48);
  float3 g_frustum_points[8] : packoffset(c49);
  float4 g_frustum_planes[6] : packoffset(c57);
  float g_orthographic : packoffset(c63);
  float g_overlay_lerp : packoffset(c63.y);
  float g_overlay_parchment_lerp : packoffset(c63.z);
  float g_overlay_show_details : packoffset(c63.w);
  float g_amount_shadow_in_far_distance : packoffset(c64);
  float2 g_camera_jitter : packoffset(c64.y);
  float2 g_prev_camera_jitter : packoffset(c65);
  uint g_debug_visualization_flags : packoffset(c65.z);
  bool g_taa_is_enabled : packoffset(c65.w);
}

cbuffer cb_environment : register(b1)
{
  float2 g_wind_direction : packoffset(c0);
  float2 g_prev_wind_direction : packoffset(c0.z);
  float2 g_wind_vector : packoffset(c1);
  float2 g_prev_wind_vector : packoffset(c1.z);
  float g_wind_gust_start_time : packoffset(c2);
  float g_wind_vegetation_multiplier : packoffset(c2.y);
  float g_prev_wind_vegetation_multiplier : packoffset(c2.z);
  float g_wind_vegetation_hstrength : packoffset(c2.w);
  float g_wind_vegetation_vstrength : packoffset(c3);
  float2 g_wind_offset : packoffset(c3.y);
  float g_snowing_intensity : packoffset(c3.w);
  int g_weather_type : packoffset(c4);
  int g_season_type : packoffset(c4.y);
  float g_snow_mask : packoffset(c4.z);
  float g_creep_intensity : packoffset(c4.w);
  float g_cubemap_sun_colour_scale : packoffset(c5);
  float g_time_of_day : packoffset(c5.y);
  float4 g_wind_map_uv_scale : packoffset(c6);
  float g_wind_map_strength : packoffset(c7);
}

cbuffer rigid_config_PS : register(b2)
{
  float g_unlit : packoffset(c0);
  float g_untextured : packoffset(c0.y);
  float g_rain : packoffset(c0.z);
  float g_tesselation_factor : packoffset(c0.w);
  float g_tesselation_displacement_scale : packoffset(c1);
  float2 g_uv2_tile_interval : packoffset(c1.y);
  float2 g_texel_density_uv_scale : packoffset(c2);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
  int g_use_secondary_lut : packoffset(c5);
  uint g_material_offset : packoffset(c5.y);
}

cbuffer TREE_BRANCH_SIM_CB : register(b3)
{
  uint debug_flag : packoffset(c0);
}

SamplerState s_wind_map_s : register(s0);
Texture2D<float2> t_wind_map : register(t0);
Texture2D<float2> t_wind_map_previous_frame : register(t1);
Texture2D<uint> t_pixel_selection_object_id_texture : register(t2);
StructuredBuffer<TREE_BRANCH_SIM_INSTANCE_DATA> g_tree_branch_sim_instance_data_srv : register(t3);


// 3Dmigoto declarations
#define cmp -


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
  float4 v14 : TEXCOORD6,
  float4 v15 : TEXCOORD7,
  uint v16 : SV_InstanceID0,
  out float4 capture_position : SV_Position0,
  out float4 capture_world : TEXCOORD0)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float2 o4 = 0;
  float p4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float3 o8 = 0;
  uint o9 = 0;
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 1 + -v6.x;
  r0.y = (uint)v14.w;
  r0.z = 9161 * v14.x;
  r0.z = frac(r0.z);
  r0.w = g_wind_map_uv_scale.y * v10.z;
  r1.x = v10.x;
  r1.x = g_wind_map_uv_scale.x * r1.x;
  r1.y = -1 * r0.w;
  r1.xy = g_wind_map_uv_scale.zw + r1.xy;
  r1.zw = t_wind_map.SampleLevel(s_wind_map_s, r1.xy, 0).xy;
  r2.x = r1.z * g_wind_map_strength + g_wind_vector.x;
  r2.y = -r1.w * g_wind_map_strength + g_wind_vector.y;
  r0.w = dot(r2.xy, r2.xy);
  r1.z = sqrt(r0.w);
  r1.w = cmp(r1.z == 0.000000);
  r0.w = rsqrt(r0.w);
  r2.xy = r2.xy * r0.ww;
  r2.xy = r1.ww ? float2(0,0) : r2.xy;
  r1.xy = t_wind_map_previous_frame.SampleLevel(s_wind_map_s, r1.xy, 0).xy;
  r3.x = r1.x * g_wind_map_strength + g_prev_wind_vector.x;
  r3.y = -r1.y * g_wind_map_strength + g_prev_wind_vector.y;
  r0.w = dot(r3.xy, r3.xy);
  r1.x = sqrt(r0.w);
  r1.y = cmp(r1.x == 0.000000);
  r0.w = rsqrt(r0.w);
  r3.xy = r3.xy * r0.ww;
  r3.xy = r1.yy ? float2(0,0) : r3.xy;
  r4.yw = v15.yy * r1.zx;
  r2.z = 0;
  r2.w = r3.x;
  r1.xyzw = r2.yzyz + -r2.zxzx;
  r5.xyzw = time_in_sec + r0.zzzz;
  r6.xyzw = float4(0.314159274,0.942477822,1.57079637,2.1991148) * r5.xxxx;
  r6.xyzw = cos(r6.xyzw);
  r0.zw = r6.xx * r6.yx;
  r0.z = r0.z * r6.z;
  r7.xyzw = float4(7.85398197,0.314159274,0.942477822,1.57079637) * r5.xyzw;
  r3.w = sin(r7.x);
  r3.w = 0.100000001 * r3.w;
  r0.z = r0.z * r6.w + -r3.w;
  r0.w = r0.w * r6.z + -r3.w;
  r5.x = 0.5 + -r0.z;
  r5.y = v15.x * r0.w;
  r4.xz = v15.xx;
  r0.zw = r5.xy * r4.xy;
  r0.z = r0.z * r4.y;
  r0.z = 0.785398185 * r0.z;
  sincos(r0.z, r4.x, r5.x);
  r1.xyzw = r4.xxxx * r1.xyzw;
  r0.z = 0.0785398185 * r0.w;
  sincos(r0.z, r4.x, r6.x);
  r8.xyzw = r4.xxxx * r2.yxxy;
  r6.yzw = r6.xxx * r1.zzw;
  r0.zw = r8.xz * r1.xz;
  r0.z = r1.y * r8.y + -r0.z;
  r1.xyz = r5.xxx * r8.zzw + r6.yzw;
  r0.w = r5.x * r6.x + -r0.w;
  r0.w = -r1.w * r8.w + r0.w;
  r3.z = 0;
  r2.xyzw = r3.yzyz + -r2.zwzw;
  r5.xyz = cos(r7.yzw);
  r3.zw = r5.xx * r5.yx;
  r1.w = r3.z * r5.z;
  r4.xy = float2(2.1991148,7.85398197) * r5.ww;
  r3.z = cos(r4.x);
  r4.x = sin(r4.y);
  r4.x = 0.100000001 * r4.x;
  r1.w = r1.w * r3.z + -r4.x;
  r3.z = r3.w * r5.z + -r4.x;
  r4.x = 0.5 + -r1.w;
  r4.y = v15.x * r3.z;
  r3.zw = r4.xy * r4.zw;
  r1.w = r3.z * r4.w;
  r1.w = 0.785398185 * r1.w;
  sincos(r1.w, r4.x, r5.x);
  r2.xyzw = r4.xxxx * r2.xyzw;
  r1.w = 0.0785398185 * r3.w;
  sincos(r1.w, r4.x, r6.x);
  r3.xyzw = r4.xxxx * r3.yxxy;
  r4.xyz = r6.xxx * r2.zzw;
  r2.xz = r3.xz * r2.xz;
  r1.w = r2.y * r3.y + -r2.x;
  r3.xyz = r5.xxx * r3.zzw + r4.xyz;
  r2.x = r5.x * r6.x + -r2.z;
  r2.x = -r2.w * r3.w + r2.x;
  r2.y = r0.z + r0.z;
  r4.xyz = r1.yzz + r1.yzz;
  r2.z = r2.y * r0.w;
  r5.xy = r4.zx * r0.ww;
  r1.xz = r4.xz * r1.xz;
  r4.xw = r0.zz * r2.yy + r1.zx;
  r6.x = r1.y * r2.y + r5.x;
  r7.x = r1.y * r4.y + -r2.z;
  r8.z = r1.y * r2.y + -r5.x;
  r0.w = r1.x + r1.z;
  r6.y = 1 + -r0.w;
  r7.y = r0.z * r4.z + r5.y;
  r8.w = r1.y * r4.y + r2.z;
  r6.z = r0.z * r4.z + -r5.y;
  r8.xy = float2(1,1) + -r4.xw;
  r0.z = r1.w + r1.w;
  r1.xyz = r3.yzz + r3.yzz;
  r0.w = r2.x * r0.z;
  r2.xy = r2.xx * r1.zx;
  r2.zw = r3.xz * r1.xz;
  r3.xz = r1.ww * r0.zz + r2.wz;
  r4.x = r3.y * r0.z + r2.x;
  r5.x = r3.y * r1.y + -r0.w;
  r9.z = r3.y * r0.z + -r2.x;
  r0.z = r2.z + r2.w;
  r4.y = 1 + -r0.z;
  r5.y = r1.w * r1.z + r2.y;
  r9.w = r3.y * r1.y + r0.w;
  r4.z = r1.w * r1.z + -r2.y;
  r9.xy = float2(1,1) + -r3.xz;
  r1.x = dot(v0.xyz, r8.xzw);
  r1.y = dot(v0.xyz, r6.xyz);
  r7.z = r8.y;
  r1.z = dot(v0.xyz, r7.xyz);
  r1.xyz = -v0.xyz + r1.xyz;
  r1.xyz = v7.www * r1.xyz + v0.xyz;
  r2.x = dot(v1.xyz, r8.xzw);
  r2.y = dot(v1.xyz, r6.xyz);
  r2.z = dot(v1.xyz, r7.xyz);
  r2.xyz = -v1.xyz + r2.xyz;
  r2.xyz = v7.www * r2.xyz + v1.xyz;
  r3.x = dot(v0.xyz, r9.xzw);
  r3.y = dot(v0.xyz, r4.xyz);
  r5.z = r9.y;
  r3.z = dot(v0.xyz, r5.xyz);
  r3.xyz = -v0.xyz + r3.xyz;
  r3.xyz = v7.www * r3.xyz + v0.xyz;
  r6.x = dot(v1.xyz, r9.xzw);
  r6.y = dot(v1.xyz, r4.xyz);
  r6.z = dot(v1.xyz, r5.xyz);
  r4.xyz = -v1.xyz + r6.xyz;
  r4.xyz = v7.www * r4.xyz + v1.xyz;
  r3.w = 1;
  r5.x = dot(v8.xyzw, r3.xyzw);
  r5.y = dot(v9.xyzw, r3.xyzw);
  r5.z = dot(v10.xyzw, r3.xyzw);
  r4.w = 1;
  r3.x = dot(v11.xyzw, r4.xyzw);
  r3.y = dot(v12.xyzw, r4.xyzw);
  r3.z = dot(v13.xyzw, r4.xyzw);
  r3.xyz = r3.xyz * r0.xxx;
  o3.xyz = v6.xxx * r5.xyz + r3.xyz;
  r1.w = 1;
  r3.x = dot(v8.xyzw, r1.xyzw);
  r3.y = dot(v9.xyzw, r1.xyzw);
  r3.z = dot(v10.xyzw, r1.xyzw);
  r2.w = 1;
  r1.x = dot(v11.xyzw, r2.xyzw);
  r1.y = dot(v12.xyzw, r2.xyzw);
  r1.z = dot(v13.xyzw, r2.xyzw);
  r1.xyz = r1.xyz * r0.xxx;
  r1.xyz = v6.xxx * r3.xyz + r1.xyz;
  r2.x = dot(v8.xyz, v2.xyz);
  r2.y = dot(v9.xyz, v2.xyz);
  r2.z = dot(v10.xyz, v2.xyz);
  r3.x = dot(v11.xyz, v2.xyz);
  r3.y = dot(v12.xyz, v2.xyz);
  r3.z = dot(v13.xyz, v2.xyz);
  r3.xyz = r3.xyz * r0.xxx;
  r2.xyz = v6.xxx * r2.xyz + r3.xyz;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  o5.xyz = r2.xyz * r0.zzz;
  r2.x = dot(v8.xyz, v3.xyz);
  r2.y = dot(v9.xyz, v3.xyz);
  r2.z = dot(v10.xyz, v3.xyz);
  r3.x = dot(v11.xyz, v3.xyz);
  r3.y = dot(v12.xyz, v3.xyz);
  r3.z = dot(v13.xyz, v3.xyz);
  r3.xyz = r3.xyz * r0.xxx;
  r2.xyz = v6.xxx * r2.xyz + r3.xyz;
  r0.z = dot(r2.xyz, r2.xyz);
  r0.z = rsqrt(r0.z);
  o6.xyz = r2.xyz * r0.zzz;
  r2.x = dot(v8.xyz, v4.xyz);
  r2.y = dot(v9.xyz, v4.xyz);
  r2.z = dot(v10.xyz, v4.xyz);
  r3.x = dot(v11.xyz, v4.xyz);
  r3.y = dot(v12.xyz, v4.xyz);
  r3.z = dot(v13.xyz, v4.xyz);
  r0.xzw = r3.xyz * r0.xxx;
  r0.xzw = v6.xxx * r2.xyz + r0.xzw;
  r2.x = dot(r0.xzw, r0.xzw);
  r2.x = rsqrt(r2.x);
  o7.xyz = r2.xxx * r0.xzw;
  r1.w = 1;
  o0.x = dot(r1.xyzw, view_projection._m00_m10_m20_m30);
  o0.y = dot(r1.xyzw, view_projection._m01_m11_m21_m31);
  o0.z = dot(r1.xyzw, view_projection._m02_m12_m22_m32);
  o0.w = dot(r1.xyzw, view_projection._m03_m13_m23_m33);
  if (g_pixel_selection_active != 0) {
    r2.x = (int)v16.x + g_pixel_selection_start_pixel_index;
    r2.yzw = float3(0,0,0);
    r0.x = t_pixel_selection_object_id_texture.Load(r2.xyz).x;
    o9.x = r0.x;
  } else {
    o9.x = -1;
  }
  r0.x = debug_flag & 1;
  r0.z = cmp((int)r0.y != -1);
  r0.y = g_tree_branch_sim_instance_data_srv[r0.y].wind_intensity;
  r2.xyz = float3(0,0,1) * r0.yyy;
  r0.yzw = r0.zzz ? r2.xyz : 0;
  o8.xyz = r0.xxx ? r0.yzw : float3(0,0,0);
  o1.xyz = v7.xyz;
  o1.w = 1 + -v14.y;
  o2.xyz = r1.xyz;
  o4.xy = v5.xy;
  p4.x = v14.z;
#if defined(RT_CAPTURE_BRANCH_UV)
  capture_world = RtWorldUv(o2.xyz, o4.xy);
#else
  capture_world = float4(o2.xyz, 1.f);
#endif
  capture_position = 0.f;
}
