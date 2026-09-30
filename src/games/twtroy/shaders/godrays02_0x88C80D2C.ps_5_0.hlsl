// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 00:45:56 2026

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

cbuffer gaussian_blur : register(b1)
{
  float4 g_input_size_ps : packoffset(c0);
}

SamplerState s_input_sampler_s : register(s0);
Texture2D<float4> t_input_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.w = -g_input_size_ps.w;
  r0.xz = float2(0,0);
  r1.xy = floor(v0.xy);
  r1.xy = g_vpos_texel_offset + r1.xy;
  r0.zw = r1.xy * g_input_size_ps.zw + r0.zw;
  r0.z = t_input_texture.SampleLevel(s_input_sampler_s, r0.zw, 0).x;
  r0.z = 0.125794396 * r0.z;
  r0.y = -2 * g_input_size_ps.w;
  r0.xy = r1.xy * g_input_size_ps.zw + r0.xy;
  r0.x = t_input_texture.SampleLevel(s_input_sampler_s, r0.xy, 0).x;
  r0.x = r0.x * 0.106482655 + r0.z;
  r0.yz = g_input_size_ps.zw * r1.xy;
  r0.yzw = t_input_texture.SampleLevel(s_input_sampler_s, r0.yz, 0).xyz;
  r0.x = r0.y * 0.132980749 + r0.x;
  o0.yz = r0.zw;
  r2.y = g_input_size_ps.w;
  r2.xz = float2(0,0);
  r0.yz = r1.xy * g_input_size_ps.zw + r2.xy;
  r0.y = t_input_texture.SampleLevel(s_input_sampler_s, r0.yz, 0).x;
  r0.x = r0.y * 0.125794396 + r0.x;
  r2.w = 2 * g_input_size_ps.w;
  r0.yz = r1.xy * g_input_size_ps.zw + r2.zw;
  r0.y = t_input_texture.SampleLevel(s_input_sampler_s, r0.yz, 0).x;
  r0.x = r0.y * 0.106482655 + r0.x;
  o0.x = 1.67354214 * r0.x;
  o0.w = 0;
  return;
}