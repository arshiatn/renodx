// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 00:05:02 2026

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
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
}

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> g_indicator_texture : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r1.x = r0.w * v3.w + -0.00392156886;
  r0.xyzw = v3.xyzw * r0.xyzw;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.zw = uiDest.xy;
  r2.xy = (uint2)r1.zw;
  r1.zw = (int2)r1.zw + int2(-1,-1);
  r2.xy = r2.xy * r1.xy;
  r3.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r1.xy = floor(r2.xy);
  r1.xy = (int2)r1.xy;
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r1.zw);
  r1.zw = float2(0,0);
  r3.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r3.w = 1;
  r1.x = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
  r1.y = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);
  r1.x = r1.x / r1.y;
  r1.y = v4.z / v4.w;
  r1.z = r1.y + -r1.x;
  r1.y = cmp(r1.x < r1.y);
  r1.x = saturate(10 + -r1.x);
  r1.w = 1.10000002 + -abs(view._m12);
  r1.w = 5 * r1.w;
  r1.w = cmp(r1.z < r1.w);
  r2.x = cmp(r1.z < 5);
  r1.z = 0.100000001 + r1.z;
  r1.z = saturate(r1.z + r1.z);
  r1.x = r1.x * -r1.z + 1;
  r1.z = r2.x ? 1 : 0.0700000003;
  r1.z = r1.w ? r1.z : 0;
  r1.y = r1.y ? r1.z : 1;
  r1.x = r1.y * r1.x;
  o0.w = r1.x * r0.w;
  o0.w = saturate(r1.x * r0.w);
  o0.xyz = float3(10, 10, 10) * r0.xyz;

  // o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
  if (HDR >= 0.5f) o0.xyz *= SI.someindicators *0.39f;
  // o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
  return;
}