// ---- Created with 3Dmigoto v1.3.16 on Wed Oct 28 07:37:08 2026
// lenseflare.hlsl vs30_flare_0 / vs30_flare_2. DLSS below 100%: the sun's occlusion query counts its pixels at the
// render size while the game expects the display's, so the occlusion (the flares' brightness and size) is divided by the
// frame's render share of the screen's area (cb12, view_share.hpp binds it for this shader).

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

cbuffer lense_flare_VS : register(b1)
{
  float sun_occlusion : packoffset(c0);
  float3 sun_position : packoffset(c0.y);
  float sun_flare : packoffset(c1);
  float4 pos_transform : packoffset(c2);
}

// 3Dmigoto declarations
#define cmp -
#include "../render_scale.hlsli"


void main(
  float4 v0 : POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float4 o1 : COLOR0,
  out float2 o2 : TEXCOORD0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  const float2 share = FrameRenderShare();
  const float occlusion = min(1.f, sun_occlusion / (share.x * share.y));

  r0.x = dot(sun_position.xyz, sun_position.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = sun_position.xyz * r0.xxx;
  r0.x = dot(view._m02_m12_m22, r0.xyz);
  r0.y = abs(r0.x) * -0.0187292993 + 0.0742610022;
  r0.y = r0.y * abs(r0.x) + -0.212114394;
  r0.y = r0.y * abs(r0.x) + 1.57072878;
  r0.z = 1 + -abs(r0.x);
  r0.x = cmp(r0.x < -r0.x);
  r0.z = sqrt(r0.z);
  r0.w = r0.y * r0.z;
  r0.w = r0.w * -2 + 3.14159274;
  r0.x = r0.x ? r0.w : 0;
  r0.x = r0.y * r0.z + r0.x;
  r0.x = 1.63702226 * r0.x;
  r0.x = min(1, r0.x);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * 0.600000024 + 0.400000006;
  r0.x = occlusion * r0.x;
  r0.y = r0.x * 1.5 + 0.5;
  r0.x = sun_flare * r0.x;
  o1.xyz = float3(0.400000006,0.400000006,0.400000006) * r0.xxx;
  r0.xy = v0.xy * r0.yy;
  r1.xyz = sun_position.xyz;
  r1.w = 1;
  r2.x = dot(r1.xyzw, view._m00_m10_m20_m30);
  r2.y = dot(r1.xyzw, view._m01_m11_m21_m31);
  r1.z = dot(r1.xyzw, view._m02_m12_m22_m32);
  r1.xy = r0.xy * float2(3600,3600) + r2.xy;
  r1.w = 1;
  o0.x = dot(r1.xyzw, projection._m00_m10_m20_m30);
  o0.y = dot(r1.xyzw, projection._m01_m11_m21_m31);
  o0.z = dot(r1.xyzw, projection._m02_m12_m22_m32);
  o0.w = dot(r1.xyzw, projection._m03_m13_m23_m33);
  o1.w = 1;
  o2.xy = v2.xy;
  return;
}
