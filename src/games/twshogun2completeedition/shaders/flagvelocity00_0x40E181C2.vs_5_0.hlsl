// rigidtextureblendwind.hlsl vs30_main / vs30_shadow (decompiled): wind-blown flags and banners drawn as instanced
// meshes (castles, camps; not cloth.hlsl), with the previous instance matrix (POSITION4-6) for the motion vectors.
// Paused, that matrix is not the current one: ATN debug21 (paused, time - prev time 0): two big banners moved 80-1500
// px in DLSS's motion vectors every frame and flickered. RenoDX: while the scene's time stands still (as
// clothvelocity00) the previous position is the current one. Moving (ships) the game's.

cbuffer camera_VS_PS : register(b0)
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

cbuffer wind_VS : register(b1)
{
  float2 g_wind_direction : packoffset(c0) = {0,0};
  float2 g_prev_wind_direction : packoffset(c0.z) = {0,0};
  float gust_start_time : packoffset(c1) = {0};
  float wind_particle_multiplier : packoffset(c1.y) = {1};
  float wind_vegetation_multiplier : packoffset(c1.z) = {0};
  float wind_vegetation_hstrength : packoffset(c1.w) = {0};
  float wind_vegetation_vstrength : packoffset(c2) = {0};
  float g_wind_influence_max_distance : packoffset(c2.y) = {300};
  float g_wind_influence_offset : packoffset(c2.z) = {200};
}

cbuffer texture_scale_VS : register(b2)
{
  float4 texture0_offset_scale : packoffset(c0);
  float4 texture1_offset_scale : packoffset(c1);
  float4 texture2_offset_scale : packoffset(c2);
  float4 texture3_offset_scale : packoffset(c3);
  float4 texture4_offset_scale : packoffset(c4);
  float4 texture5_offset_scale : packoffset(c5);
  float4 texture6_offset_scale : packoffset(c6);
  float4 texture7_offset_scale : packoffset(c7);
  float wind_offset : packoffset(c8);
}



// 3Dmigoto declarations
#define cmp -


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
  float4 v9 : POSITION4,
  float4 v10 : POSITION5,
  float4 v11 : POSITION6,
  float4 v12 : TEXCOORD1,
  float v13 : TEXCOORD2,
  out float4 o0 : SV_Position0,
  out float4 o1 : TEXCOORD0,
  out float4 o2 : TEXCOORD1,
  out float4 o3 : COLOR0,
  out float4 o4 : TEXCOORD8,
  out float4 o5 : TEXCOORD2,
  out float4 o6 : TEXCOORD3,
  out float4 o7 : TEXCOORD4,
  out float4 o8 : TEXCOORD5,
  out float2 o9 : COLOR1,
  out float4 o10 : TEXCOORD6,
  out float4 o11 : TEXCOORD7)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.y = 0;
  r1.y = 0;
  r0.w = 20 * wind_offset;
  r1.w = wind_offset * 0.5 + 0.75;
  r2.xyzw = float4(time_in_sec, time_in_sec, time_in_sec, prev_time_in_sec) * r1.wwww + r0.wwww;
  r3.xyz = float3(10,5,10) * r2.xyw;
  r4.xyz = v0.xyz;
  r4.w = 1;
  r5.y = dot(r4.xyzw, v7.xyzw);
  r3.xy = r5.yy * float2(10,100) + r3.xy;
  r3.xy = sin(r3.xy);
  r3.xw = g_wind_direction.xy * r3.xx;
  r3.xy = r3.xw * r3.yy;
  r0.xz = float2(0.100000001,0.100000001) * r3.xy;
  r6.xyzw = float4(g_wind_direction, g_prev_wind_direction) * v5.wwww;
  r0.w = r2.z * 20 + r5.y;
  r0.w = sin(r0.w);
  r0.w = r0.w * 0.5 + 0.5;
  r2.yz = r6.xy * r0.ww;
  r1.xz = float2(0.349999994,0.349999994) * r2.yz;
  r0.xyz = r0.xyz * v5.www + r1.xyz;
  r1.xy = r2.xw * float2(10,10) + v0.xx;
  r1.xy = sin(r1.xy);
  r1.xy = float2(0.0700000003,0.0700000003) * r1.xy;
  r0.xyz = r1.xxx * v5.www + r0.xyz;
  r5.x = dot(r4.xyzw, v6.xyzw);
  r5.z = dot(r4.xyzw, v8.xyzw);
  r0.xyz = r0.xyz * v13.xxx + r5.xyz;
  r0.w = 1;
  r5.x = dot(r0.xyzw, view_projection._m00_m10_m20_m30);
  r5.y = dot(r0.xyzw, view_projection._m01_m11_m21_m31);
  r5.z = dot(r0.xyzw, view_projection._m02_m12_m22_m32);
  r5.w = dot(r0.xyzw, view_projection._m03_m13_m23_m33);
  o0.xyzw = r5.xyzw;
  o3.xyzw = r5.xyzw;
  o1.xyzw = r0.xyzw;
  o11.w = dot(r0.xyzw, view._m02_m12_m22_m32);
  r0.xyz = v2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o2.x = dot(r0.xyz, v6.xyz);
  o2.y = dot(r0.xyz, v7.xyz);
  o2.z = dot(r0.xyz, v8.xyz);
  o2.w = v1.y;
  r0.x = 5 * r2.w;
  r2.y = dot(r4.xyzw, v10.xyzw);
  r0.x = r2.y * 100 + r0.x;
  r0.y = r2.y * 10 + r3.z;
  r0.xy = sin(r0.xy);
  r0.yz = g_prev_wind_direction.xy * r0.yy;
  r0.xy = r0.yz * r0.xx;
  r0.xz = float2(0.100000001,0.100000001) * r0.xy;
  r0.w = r2.w * 20 + r2.y;
  r0.w = sin(r0.w);
  r0.w = r0.w * 0.5 + 0.5;
  r1.xz = r6.zw * r0.ww;
  r3.xz = float2(0.349999994,0.349999994) * r1.xz;
  r0.y = 0;
  r3.y = 0;
  r0.xyz = r0.xyz * v5.www + r3.xyz;
  r0.xyz = r1.yyy * v5.www + r0.xyz;
  r2.x = dot(r4.xyzw, v9.xyzw);
  r2.z = dot(r4.xyzw, v11.xyzw);
  o4.xyz = r0.xyz * v13.xxx + r2.xyz;
  o4.w = 1;
  // RenoDX: paused, the previous position is the current one.
  if (abs(time_in_sec - prev_time_in_sec) < 1e-3f) o4.xyz = o1.xyz;
  o5.xy = texture0_offset_scale.zw * v1.xy + texture0_offset_scale.xy;
  o5.zw = texture1_offset_scale.zw * v1.xy + texture1_offset_scale.xy;
  o6.xy = texture2_offset_scale.zw * v1.xy + texture2_offset_scale.xy;
  o6.zw = texture3_offset_scale.zw * v1.xy + texture3_offset_scale.xy;
  o7.xy = texture4_offset_scale.zw * v1.xy + texture4_offset_scale.xy;
  o7.zw = v1.xy;
  o8.xy = texture5_offset_scale.zw * v1.xy + texture5_offset_scale.xy;
  o8.zw = texture6_offset_scale.zw * v1.xy + texture6_offset_scale.xy;
  o9.xy = texture7_offset_scale.zw * v1.xy + texture7_offset_scale.xy;
  r0.x = floor(v12.w);
  o10.w = 0.00100000005 * r0.x;
  r0.xyz = v3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o10.x = dot(r0.xyz, v6.xyz);
  o10.y = dot(r0.xyz, v7.xyz);
  o10.z = dot(r0.xyz, v8.xyz);
  r0.xyz = v4.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o11.x = dot(r0.xyz, v6.xyz);
  o11.y = dot(r0.xyz, v7.xyz);
  o11.z = dot(r0.xyz, v8.xyz);
  return;
}
