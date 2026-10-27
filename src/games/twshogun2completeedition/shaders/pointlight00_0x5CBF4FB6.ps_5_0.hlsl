// pointlight.hlsl ps30_main_highdef: deferred point lights (fire, torches).
// Render scale (DLSS addon): the CPU's g_viewport_offset_scale stays the full screen while the 3D passes draw into
// the top-left render scale part: the viewport comes from the camera instead (g_viewport_dimensions, patched to the
// render size by the DLSS addon; the game's own "respect the viewport" formula). At full size the same as the game's.
// HDR: x VFX Brightness: Firelight (v24).
#include "../shared.h"

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

cbuffer lighting_VS_PS : register(b1)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float sun_specular : packoffset(c1.w);
  float g_cube_ambient_influence : packoffset(c2);
  float3 ambient_cube_lr[2] : packoffset(c3);
  float3 ambient_cube_tb[2] : packoffset(c5);
  float3 ambient_cube_fb[2] : packoffset(c7);
  float3 g_deep_water_colour : packoffset(c9);
  float3 g_shallow_water_colour : packoffset(c10);
  bool g_use_spherical_harmonics : packoffset(c10.w);
  float2 g_cloud_shadow_direction : packoffset(c11);
  float g_cloud_shadow_speed : packoffset(c11.z);
  float g_cloud_shadow_scale : packoffset(c11.w);
  float g_cloud_shadow_lerp : packoffset(c12);
  float4 g_playable_area_bounds : packoffset(c13);
  float g_vegetation_wrap_lighting_bias : packoffset(c14);
  float g_ambient_fudge_factor : packoffset(c14.y);
  float g_time_of_day_unary : packoffset(c14.z);
  float g_SSAO_low_clamp : packoffset(c14.w);
  float g_Shadow_low_clamp : packoffset(c15);
  float g_light_scale_for_units : packoffset(c15.y);
  float4 g_water_reflection_tint : packoffset(c16);
  float g_water_reflection_strength : packoffset(c17);
}

cbuffer point_light_PS : register(b2)
{
  float3 g_light_position : packoffset(c0);
  float3 g_light_colour : packoffset(c1);
  float g_light_radius : packoffset(c1.w);
  float4 g_viewport_offset_scale : packoffset(c2);
  float g_clip : packoffset(c3);
}

Texture2D<float4> gbuffer_normal_texture : register(t0);
Texture2D<float4> gbuffer_colour_texture : register(t1);
Texture2D<float4> gbuffer_constants_texture : register(t2);
Texture2D<float4> gbuffer_readable_depth_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 != g_clip);
  r0.y = v1.z / v1.w;
  r0.zw = screen_size.zw * v0.xy;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r0.zw = r1.zw * r0.zw;
  r0.zw = floor(r0.zw);
  r0.zw = (int2)r0.zw;
  r0.zw = max(int2(0,0), (int2)r0.zw);
  r1.xy = min((int2)r0.zw, (int2)r1.xy);
  r1.zw = float2(0,0);
  r2.z = gbuffer_readable_depth_texture.Load(r1.xyz).x;
  r0.y = r2.z + -r0.y;
  r0.y = cmp(r0.y < 0);
  r0.x = r0.x ? r0.y : 0;
  if (r0.x != 0) discard;
  r0.xy = (v0.xy * screen_size.zw * g_render_target_dimensions.xy - g_viewport_origin) * g_viewport_dimensions.zw;
  r2.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r0.x = dot(r2.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r2.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r2.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r2.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r2.xyz = -g_light_position.xyz + r0.xyz;
  r0.xyz = -camera_position.xyz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r0.xyz * r0.www;
  r1.z = dot(r2.xyz, r2.xyz);
  r2.w = rsqrt(r1.z);
  r1.z = sqrt(r1.z);
  r1.z = saturate(r1.z / g_light_radius);
  r1.z = 1 + -r1.z;
  r4.xyz = g_light_colour.xyz * r1.zzz;
  r5.xyz = -r2.xyz * r2.www + -r3.xyz;
  r2.xyz = r2.xyz * r2.www;
  r1.z = dot(r5.xyz, r5.xyz);
  r1.z = rsqrt(r1.z);
  r5.xyz = r5.xyz * r1.zzz;
  r6.xy = gbuffer_normal_texture.Load(r1.xyw).xy;
  r6.xy = r6.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
  sincos(r6.xy, r6.xy, r7.xy);
  r8.x = r7.y;
  r8.z = r6.y;
  r7.xz = r8.xz * r7.xx;
  r7.y = r6.x;
  r1.z = dot(r7.xyz, r5.xyz);
  r1.z = r1.z * r1.z;
  r5.xyz = gbuffer_constants_texture.Load(r1.xyw).xyw;
  r1.xyw = gbuffer_colour_texture.Load(r1.xyw).xyz;
  r5.yw = float2(1,1) + -r5.xy;
  r2.w = max(0, r5.w);
  r6.xyz = r5.yyy * r1.xyw;
  r1.xyw = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r1.xyw;
  r1.xyw = r5.xxx * r1.xyw + float3(0.0399999991,0.0399999991,0.0399999991);
  r3.w = 255 * r5.z;
  r3.w = round(r3.w);
  r3.w = (uint)r3.w;
  r3.w = (int)r3.w & 64;
  r5.xyz = float3(0.959999979,0.959999979,0.959999979) * r6.xyz;
  r6.xy = min(float2(1,0.200000003), r2.ww);
  r2.w = cmp(0 < sun_specular);
  r2.w = r2.w ? r6.x : r6.y;
  r4.w = r6.x * -1.12524891 + 1.12839997;
  r2.w = 1 + -r2.w;
  r5.w = r2.w * r2.w;
  r2.w = 1.57079637 * r2.w;
  r2.w = sin(r2.w);
  r6.x = r5.w * r5.w + -1;
  r1.z = r1.z * r6.x + 1;
  r1.z = r1.z * r1.z;
  r1.z = 3.14159274 * r1.z;
  r6.x = r5.w * r5.w;
  r1.z = r6.x / r1.z;
  r6.x = 0.5 * r5.w;
  r5.w = -r5.w * 0.5 + 1;
  r6.y = saturate(dot(r7.xyz, -r3.xyz));
  r3.x = dot(r7.xyz, r3.xyz);
  r3.y = dot(r7.xyz, -r2.xyz);
  r3.x = max(-1, r3.x);
  r3.z = r6.y * r5.w + r6.x;
  r3.z = 1 / r3.z;
  r6.y = saturate(r3.y);
  r3.y = max(-1, r3.y);
  r3.xy = min(float2(1,1), r3.xy);
  r5.w = r6.y * r5.w + r6.x;
  r5.w = 1 / r5.w;
  r3.z = r5.w * r3.z;
  r3.z = 0.25 * r3.z;
  r3.z = min(1, r3.z);
  r5.w = -1 + r6.y;
  r2.w = r2.w * r5.w + 1;
  r2.w = r3.z * r2.w;
  r1.z = r2.w * r1.z;
  r0.xyz = r0.xyz * r0.www + -r2.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, -r2.xyz));
  r0.y = r0.x * -5.55472994 + -6.98316002;
  r0.x = r0.y * r0.x;
  r0.x = exp2(r0.x);
  r0.yzw = float3(1,1,1) + -r1.xyw;
  r2.xyz = r0.yzw * r0.xxx + r1.xyw;
  r2.xyz = r2.xyz * r1.zzz;
  r0.x = abs(r3.x) * -0.0187292993 + 0.0742610022;
  r0.x = r0.x * abs(r3.x) + -0.212114394;
  r0.x = r0.x * abs(r3.x) + 1.57072878;
  r1.z = 1 + -abs(r3.x);
  r2.w = cmp(r3.x < -r3.x);
  r1.z = sqrt(r1.z);
  r3.x = r1.z * r0.x;
  r3.x = r3.x * -2 + 3.14159274;
  r2.w = r2.w ? r3.x : 0;
  r0.x = r0.x * r1.z + r2.w;
  r1.z = abs(r3.y) * -0.0187292993 + 0.0742610022;
  r1.z = r1.z * abs(r3.y) + -0.212114394;
  r1.z = r1.z * abs(r3.y) + 1.57072878;
  r2.w = 1 + -abs(r3.y);
  r2.w = sqrt(r2.w);
  r3.x = r2.w * r1.z;
  r3.x = r3.x * -2 + 3.14159274;
  r3.z = cmp(r3.y < -r3.y);
  r3.y = max(0, r3.y);
  r3.x = r3.z ? r3.x : 0;
  r1.z = r1.z * r2.w + r3.x;
  r2.w = max(r1.z, r0.x);
  r2.w = sin(r2.w);
  r3.x = r1.z + -r0.x;
  r0.x = min(r1.z, r0.x);
  sincos(r0.x, r0.x, r6.x);
  r0.x = r0.x / r6.x;
  r1.z = cos(r3.x);
  r1.z = max(0, r1.z);
  r3.x = r4.w * r4.w;
  r6.xz = r4.ww * r4.ww + float2(0.330000013,0.0900000036);
  r3.z = 0.449999988 * r3.x;
  r3.z = r3.z / r6.z;
  r4.w = r6.x + r6.x;
  r3.x = r3.x / r4.w;
  r3.x = 1 + -r3.x;
  r1.z = r3.z * r1.z;
  r1.z = r1.z * r2.w;
  r0.x = r1.z * r0.x + r3.x;
  r0.x = r0.x * r3.y;
  r1.z = cmp(0.5 < g_camera_temp0.x);
  r0.x = r1.z ? r6.y : r0.x;
  r1.z = r6.y * -5.55472994 + -6.98316002;
  r1.z = r1.z * r6.y;
  r1.z = exp2(r1.z);
  r1.xyz = r0.yzw * r1.zzz + r1.xyw;
  r0.yzw = max(float3(0.00100000005,0.00100000005,0.00100000005), r0.yzw);
  r0.yzw = r5.xyz / r0.yzw;
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r1.xyz = r4.xyz * r1.xyz;
  r0.yzw = r1.xyz * r0.yzw;
  r0.xyz = r0.yzw * r0.xxx;
  r0.xyz = r3.www ? float3(1,0,0) : r0.xyz;
  o0.xyz = r2.xyz * r4.xyz + r0.xyz;
  if (HDR >= 1.f) o0.xyz *= SI.vfx_firelight;  // VFX Brightness: Firelight
  o0.w = 1;
  return;
}