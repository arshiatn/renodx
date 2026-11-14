// pointlightsmoothfalloff slm (D96A8DC3, VS D2A0FF23: a light an instance, POSITION1 its colour, POSITION2 its place and
// reach): the game's, x the light's traced visibility (Torch and Fire Shadows: twattila/rt_scene.hpp InstallPointLights,
// twattila/rt_local_lights.hlsli). Runs only with the rays' mask bound (addon.cpp PointLightRays); otherwise the game's
// own bytes do.
#include "../../twattila/rt_local_lights.hlsli"

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

cbuffer dynamic_light_PS : register(b1)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  r0.xy = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = r0.xy / g_viewport_offset_scale.zw;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.xy = uiDest.xy;
  r2.xy = (uint2)r0.xy;
  r0.xy = (int2)r0.xy + int2(-1,-1);
  r0.zw = r2.xy * r0.zw;
  r0.zw = floor(r0.zw);
  r0.zw = (int2)r0.zw;
  r0.zw = max(int2(0,0), (int2)r0.zw);
  r0.xy = min((int2)r0.zw, (int2)r0.xy);
  r0.zw = float2(0,0);
  r1.z = gbuffer_channel_4_texture.Load(r0.xyz).x;
  r1.w = 1;
  r2.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r2.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r2.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r2.xyz / r0.zzz;
  r2.xyz = v3.xyz + -r1.xyz;
  r1.xyz = -camera_position.xyz + r1.xyz;
  r0.z = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r0.z);
  r0.z = sqrt(r0.z);
  r0.z = saturate(r0.z / v3.w);
  r0.z = 1 + -r0.z;
  r0.z = r0.z * r0.z;
  r3.xyz = r2.xyz * r1.www;
  r4.xyz = gbuffer_channel_1_texture.Load(r0.xyw).xyz;
  r4.xy = r4.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
  r2.w = 1 + -r4.z;
  sincos(r4.xy, r4.xy, r5.xy);
  r6.x = r5.y;
  r6.z = r4.y;
  r5.xz = r6.xz * r5.xx;
  r5.y = r4.x;
  r3.w = dot(r5.xyz, r3.xyz);
  r4.xyz = r5.xyz * r3.www + -r3.xyz;
  r3.w = dot(r4.xyz, r4.xyz);
  r3.w = rsqrt(r3.w);
  r4.xyz = r4.xyz * r3.www;
  r6.xyzw = gbuffer_channel_3_texture.Load(r0.xyw).xyzw;
  r0.xyw = gbuffer_channel_2_texture.Load(r0.xyw).xyz;
  r3.w = 255 * r6.w;
  r3.w = round(r3.w);
  r3.w = (uint)r3.w;
  r3.w = (int)r3.w & 2;
  r4.xyz = r3.www ? r4.xyz : r5.xyz;
  r3.w = dot(r1.xyz, r1.xyz);
  r3.w = rsqrt(r3.w);
  r1.xyz = r3.www * r1.xyz;
  r3.w = dot(r1.xyz, r4.xyz);
  r4.w = r3.w + r3.w;
  r3.w = saturate(r3.w);
  r5.xyz = r4.xyz * -r4.www + r1.xyz;
  r4.x = dot(r4.xyz, r3.xyz);
  r3.x = dot(r3.xyz, r5.xyz);
  r3.x = max(-1, r3.x);
  r3.x = min(1, r3.x);
  r3.y = max(0, r4.x);
  r3.z = abs(r3.x) * -0.0187292993 + 0.0742610022;
  r3.z = r3.z * abs(r3.x) + -0.212114394;
  r3.z = r3.z * abs(r3.x) + 1.57072878;
  r4.x = 1 + -abs(r3.x);
  r3.x = cmp(r3.x < -r3.x);
  r4.x = sqrt(r4.x);
  r4.y = r4.x * r3.z;
  r4.y = r4.y * -2 + 3.14159274;
  r3.x = r3.x ? r4.y : 0;
  r3.x = r3.z * r4.x + r3.x;
  r4.xyz = float3(-0.00872664619,0.00872664619,0.00872664619) + r3.xxx;
  r3.x = r2.w * r2.w;
  r3.z = r3.x * r2.w;
  r4.w = r3.z * r2.w;
  r5.x = r4.w * r2.w;
  r4.w = 64.5780029 * r4.w;
  r4.w = r5.x * 28.0900002 + -r4.w;
  r3.z = r3.z * 48.6290016 + r4.w;
  r3.x = -r3.x * 12.6590004 + r3.z;
  r2.w = saturate(r2.w * 1.54589999 + r3.x);
  r2.w = 1 + -r2.w;
  r3.x = r2.w * -1.12524891 + 1.12839997;
  r2.w = 1 + -r2.w;
  r3.x = 1 / r3.x;
  r4.xyz = r4.xyz * r3.xxx;
  r3.z = cmp(0 < r4.x);
  r4.w = cmp(r4.x < 0);
  r3.z = (int)-r3.z + (int)r4.w;
  r3.z = (int)r3.z;
  r4.xy = float2(0.707106769,0.707106769) * r4.xy;
  r4.z = r4.z * 0.196853578 + 1;
  r4.w = abs(r4.x) * 0.278393 + 1;
  r5.x = abs(r4.x) * abs(r4.x);
  r4.w = r5.x * 0.230388999 + r4.w;
  r5.x = r5.x * abs(r4.x);
  r4.w = r5.x * 0.000972000009 + r4.w;
  r4.x = r5.x * abs(r4.x);
  r4.x = r4.x * 0.0781079978 + r4.w;
  r4.x = r4.x * r4.x;
  r4.x = r4.x * r4.x;
  r4.x = 1 / r4.x;
  r4.x = 1 + -r4.x;
  r3.z = r3.z * r4.x + 1;
  r3.z = 0.5 * r3.z;
  r4.x = r4.y * r4.y;
  r4.z = r4.x * 0.230388999 + r4.z;
  r4.x = r4.x * r4.y;
  r4.y = r4.x * r4.y;
  r4.x = r4.x * 0.000972000009 + r4.z;
  r4.x = r4.y * 0.0781079978 + r4.x;
  r4.x = r4.x * r4.x;
  r4.x = r4.x * r4.x;
  r4.x = 1 / r4.x;
  r4.x = 2 + -r4.x;
  r3.z = r4.x * 0.5 + -r3.z;
  r4.x = r3.x * 0.309216887 + 1;
  r4.yz = float2(-1.11072075,1.11072075) * r3.xx;
  r5.xy = r4.yz * r4.yz;
  r4.xw = r5.xy * float2(0.230388999,0.230388999) + r4.xx;
  r3.x = r5.x * -r4.y;
  r5.x = r5.y * r4.z;
  r4.x = r3.x * 0.000972000009 + r4.x;
  r4.w = r5.x * 0.000972000009 + r4.w;
  r4.z = r5.x * r4.z;
  r3.x = r3.x * -r4.y;
  r3.x = r3.x * 0.0781079978 + r4.x;
  r3.x = r3.x * r3.x;
  r3.x = r3.x * r3.x;
  r3.x = 1 / r3.x;
  r3.x = 1 + -r3.x;
  r3.x = 1 + -r3.x;
  r3.x = 0.5 * r3.x;
  r4.x = r4.z * 0.0781079978 + r4.w;
  r4.x = r4.x * r4.x;
  r4.x = r4.x * r4.x;
  r4.x = 1 / r4.x;
  r4.x = 2 + -r4.x;
  r3.x = r4.x * 0.5 + -r3.x;
  r3.x = abs(r3.z) / r3.x;
  r3.x = min(1, r3.x);
  r3.x = r3.x * r3.x;
  r3.z = r2.w * r2.w;
  r2.w = 1.57079637 * r2.w;
  r2.w = sin(r2.w);
  r4.x = 0.5 * r3.z;
  r3.z = -r3.z * 0.5 + 1;
  r3.w = r3.w * r3.z + r4.x;
  r3.w = 1 / r3.w;
  r4.y = min(1, r3.y);
  r0.xyw = r3.yyy * r0.xyw;
  r0.xyw = v2.xyz * r0.xyw;
  r3.y = r4.y * r3.z + r4.x;
  r3.z = -1 + r4.y;
  r2.w = r2.w * r3.z + 1;
  r3.y = 1 / r3.y;
  r3.y = r3.y * r3.w;
  r3.xy = float2(0.785398185,0.25) * r3.xy;
  r3.y = min(1, r3.y);
  r2.w = r3.y * r2.w;
  r2.w = r3.x * r2.w;
  r2.xyz = r2.xyz * r1.www + -r1.xyz;
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = r2.xyz * r1.www;
  r1.x = saturate(dot(-r1.xyz, r2.xyz));
  r1.y = r1.x * -5.55472994 + -6.98316002;
  r1.x = r1.y * r1.x;
  r1.x = exp2(r1.x);
  r1.yzw = float3(1,1,1) + -r6.xyz;
  r1.xyz = r1.yzw * r1.xxx + r6.xyz;
  r1.xyz = r2.www * r1.xyz;
  r2.xyz = max(r1.xyz, r6.xyz);
  r2.xyz = float3(1,1,1) + -r2.xyz;
  r0.xyw = r2.xyz * r0.xyw;
  r0.xyw = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r0.xyw;
  r0.xyw = r1.xyz * v2.xyz + r0.xyw;
  r0.xyz = r0.zzz * r0.xyw;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  o0.w = 1;
  return;
}
