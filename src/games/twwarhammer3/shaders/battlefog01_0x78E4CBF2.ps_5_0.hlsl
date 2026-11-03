// 0x78E4CBF2 (fx_battlefog): the half-resolution fog light (0xA8F5202D's output, depth in its alpha) up to full
// resolution, nearest in depth, added to the picture. SI.fog_strength: x the light, at 1 the game's.

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

SamplerState s_halfrez_fog_accumulation_buffer_s : register(s0);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> t_halfrez_fog_accumulation_buffer : register(t1);


#include "../shared.h"

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy * g_screen_size.zw + -g_screen_size.zw;
  r0.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r0.xy, 0).xyzw;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (int2)r1.xy + int2(-1,-1);
  r1.xy = (uint2)r1.xy;
  r2.xy = g_screen_size.zw * v0.xy;
  r1.xy = r2.xy * r1.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r1.zw);
  r1.zw = float2(0,0);
  r1.x = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r1.y = camera_near_far.x + -camera_near_far.y;
  r1.x = r1.x * r1.y + camera_near_far.y;
  r1.y = camera_near_far.x * camera_near_far.y;
  r1.x = r1.y / r1.x;
  r1.y = r1.x + -r0.w;
  r1.z = cmp(abs(r1.y) < 3.40282347e+038);
  r1.y = min(3.40282347e+038, abs(r1.y));
  r0.xyzw = r1.zzzz ? r0.xyzw : 0;
  r3.xyzw = g_screen_size.zwzw * float4(0,-1,1,-1) + r2.xyxy;
  r4.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r3.xy, 0).xyzw;
  r3.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r3.zw, 0).xyzw;
  r1.z = -r4.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r4.xyzw : r0.xyzw;
  r1.z = -r3.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r3.xyzw : r0.xyzw;
  r3.xyzw = g_screen_size.zwzw * float4(-1,0,1,0) + r2.xyxy;
  r4.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r3.xy, 0).xyzw;
  r3.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r3.zw, 0).xyzw;
  r1.z = -r4.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r4.xyzw : r0.xyzw;
  r4.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r2.xy, 0).xyzw;
  r2.xyzw = g_screen_size.zwzw * float4(-1,1,0,1) + r2.xyxy;
  r1.z = -r4.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r4.xyzw : r0.xyzw;
  r1.z = -r3.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r3.xyzw : r0.xyzw;
  r3.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r2.xy, 0).xyzw;
  r2.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r2.zw, 0).xyzw;
  r1.z = -r3.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r3.xyzw : r0.xyzw;
  r1.z = -r2.w + r1.x;
  r1.w = cmp(abs(r1.z) < r1.y);
  r1.y = min(r1.y, abs(r1.z));
  r0.xyzw = r1.wwww ? r2.xyzw : r0.xyzw;
  r1.zw = v0.xy * g_screen_size.zw + g_screen_size.zw;
  r2.xyzw = t_halfrez_fog_accumulation_buffer.SampleLevel(s_halfrez_fog_accumulation_buffer_s, r1.zw, 0).xyzw;
  r1.x = -r2.w + r1.x;
  r1.x = cmp(abs(r1.x) < r1.y);
  o0.xyzw = r1.xxxx ? r2.xyzw : r0.xyzw;
  o0.xyz *= SI.fog_strength;
  return;
}
