// PHARAOH highlightindicator03 - spline / glow indicator (0x0051B762). Three scrolling
// texture layers, a global mask, a normal fade, a camera + cursor distance fade and a
// pulse, tinted by sp_colour * sp_glow_strength.
//
// TWO DECOMPILER BUGS.
//
// 1. asm 37   mad r1.xyzw, cb0[26].xxxx, cb1[10].xyzw, r0.yzyz
//    cb1[10] holds TWO float2 fields - sp_scrolling_colour_scroll in .xy and
//    sp_scrolling_colour_scroll2 in .zw. 3Dmigoto named all four after the first, so the
//    dump says sp_scrolling_colour_scroll.xyzw: four components of a float2. That will not
//    compile, and had it compiled, layer B would have scrolled at layer A's speed - the
//    two layers would drift in lockstep instead of interfering.
//
// 2. "nointerpolation" dropped from the NORMAL1 input, which is a uint (ISGN: register 13,
//    mask .x). HLSL requires integer pixel shader inputs to be nointerpolation. Same miss
//    as fire00. Neither NORMAL1 nor OBJECT_POS0 is read here, but both stay declared, in
//    this order, so register 13 packs the way the vertex shader wrote it.
//
// I scanned all 11 instructions that read 2+ components of one cbuffer row; only asm 37
// spans two fields. The other ten are single float2/float3 fields and decompiled fine.
//
// Brightness follows highlightindicator02 exactly: saturate restores the write clamp the
// FP16 target no longer provides - in SDR as well as HDR, since the upgrade applies either
// way - and SI.someindicators stays HDR-only. 1.0 on the slider is exactly stock.

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
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
}

cbuffer shader_params : register(b1)
{
  float4 sp_Texture_A_channels : packoffset(c0);
  float4 sp_Texture_B_Channels : packoffset(c1);
  float4 sp_Texture_C_Channels : packoffset(c2);
  float sp_camera_fade_switch : packoffset(c3);
  float3 sp_colour : packoffset(c3.y);
  float2 sp_colour_overlay_pan : packoffset(c4);
  float sp_colour_overlay_strength : packoffset(c4.z);
  float2 sp_colour_overlay_tile : packoffset(c5);
  float3 sp_cursor_position : packoffset(c6);
  float2 sp_global_mask_pan : packoffset(c7);
  float2 sp_global_mask_tiling : packoffset(c7.z);
  float sp_glow_strength : packoffset(c8);
  float sp_normal_fade_in_out : packoffset(c8.y);
  float sp_normal_fade_power : packoffset(c8.z);
  float sp_pulse_effect_amount : packoffset(c8.w);
  float sp_scrolling_brightness : packoffset(c9);
  float sp_scrolling_brightness2 : packoffset(c9.y);
  float sp_scrolling_brightness3 : packoffset(c9.z);
  float2 sp_scrolling_colour_scroll : packoffset(c10);
  float2 sp_scrolling_colour_scroll2 : packoffset(c10.z);
  float2 sp_scrolling_colour_scroll3 : packoffset(c11);
  float2 sp_scrolling_colour_tiling : packoffset(c11.z);
  float2 sp_scrolling_colour_tiling2 : packoffset(c12);
  float2 sp_scrolling_colour_tiling3 : packoffset(c12.z);
  float sp_spline_length : packoffset(c13);
  float sp_total_alpha_strength : packoffset(c13.y);
}

SamplerState s_Texture_A_s : register(s0);
SamplerState s_Texture_B_s : register(s1);
SamplerState s_Texture_C_s : register(s2);
SamplerState s_global_mask_s : register(s3);
SamplerState s_colour_overlay_s : register(s4);
Texture2D<float4> t_Texture_A : register(t0);
Texture2D<float4> t_Texture_B : register(t1);
Texture2D<float4> t_Texture_C : register(t2);
Texture2D<float4> t_global_mask : register(t3);
Texture2D<float4> t_colour_overlay : register(t4);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  float3 v9 : TEXCOORD8,
  float4 v10 : COLOR1,
  float4 v11 : COLOR2,
  float4 v12 : NORMAL0,
  nointerpolation uint v13 : NORMAL1,      // FIXED: uint PS inputs must be nointerpolation
  nointerpolation float3 w13 : OBJECT_POS0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = camera_position.xyz + -v1.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = dot(v3.xyz, v3.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v3.xyz * r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.y = log2(abs(r0.x));
  r0.x = 1 + -abs(r0.x);
  r0.x = log2(r0.x);
  r0.xy = sp_normal_fade_power * r0.xy;
  r0.x = exp2(r0.x);
  r0.y = exp2(r0.y);
  r0.y = r0.y + -r0.x;
  r0.x = sp_normal_fade_in_out * r0.y + r0.x;
  r1.x = sp_spline_length * v2.x;
  r1.y = v2.y;
  r0.yz = float2(0.100000001,0.100000001) * r1.xy;
  // FIXED: cb1[10] is two fields. .xy is layer A's scroll, .zw is layer B's.
  r1.xy = time_in_sec * sp_scrolling_colour_scroll.xy  + r0.yz;
  r1.zw = time_in_sec * sp_scrolling_colour_scroll2.xy + r0.yz;
  r1.xy = sp_scrolling_colour_tiling.xy * r1.xy;
  r1.zw = sp_scrolling_colour_tiling2.xy * r1.zw;
  r2.xyzw = t_Texture_B.Sample(s_Texture_B_s, r1.zw).xyzw;
  r0.w = dot(sp_Texture_B_Channels.xyzw, r2.xyzw);
  r0.w = sp_scrolling_brightness2 * r0.w;
  r1.xyzw = t_Texture_A.Sample(s_Texture_A_s, r1.xy).xyzw;
  r1.x = dot(sp_Texture_A_channels.xyzw, r1.xyzw);
  r1.x = sp_scrolling_brightness * r1.x;
  r0.w = r1.x * r0.w;
  r1.xy = time_in_sec * sp_scrolling_colour_scroll3.xy + r0.yz;
  r0.yz = time_in_sec * sp_colour_overlay_pan.xy + r0.yz;
  r0.yz = sp_colour_overlay_tile.xy * r0.yz;
  r2.xyzw = t_colour_overlay.Sample(s_colour_overlay_s, r0.yz).xyzw;
  r0.yz = sp_scrolling_colour_tiling3.xy * r1.xy;
  r1.xyzw = t_Texture_C.Sample(s_Texture_C_s, r0.yz).xyzw;
  r0.y = dot(sp_Texture_C_Channels.xyzw, r1.xyzw);
  r0.y = sp_scrolling_brightness3 * r0.y;
  r0.y = r0.w * r0.y;
  r0.x = r0.x * r0.y;
  r0.y = time_in_sec * sp_global_mask_pan.y + -v2.y;
  r0.y = sp_global_mask_tiling.y * r0.y;
  r1.y = 0.949999988 * r0.y;
  r0.y = sp_global_mask_pan.x * time_in_sec;
  r1.x = sp_global_mask_tiling.x * r0.y;
  r0.y = t_global_mask.Sample(s_global_mask_s, r1.xy).x;
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y;
  r0.z = 0.100000001 * r0.y;
  r0.x = r0.x * r0.y + r0.z;
  r0.x = sp_total_alpha_strength * r0.x;
  r0.x = max(0, r0.x);
  r0.yzw = -camera_position.xyz + v1.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.y = -100 + r0.y;
  r0.y = saturate(0.00999999978 * r0.y);
  r1.xyz = -sp_cursor_position.xyz + v1.xyz;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = sqrt(r0.z);
  r0.z = -50 + r0.z;
  r0.z = saturate(0.0199999996 * r0.z);
  r0.yz = float2(1,1) + -r0.yz;
  r0.y = max(r0.y, r0.z);
  r0.y = -1 + r0.y;
  r0.y = sp_camera_fade_switch * r0.y + 1;
  r0.x = r0.x * r0.y;
  r0.y = 5 * time_in_sec;
  r0.y = sin(r0.y);
  r0.y = r0.y * 0.5 + -0.5;
  r0.y = sp_pulse_effect_amount * r0.y + 1;
  r0.y = max(0.200000003, r0.y);
  r0.y = min(1, r0.y);
  r0.x = r0.y * r0.x;
  r0.yzw = r2.xyz * sp_colour_overlay_strength + -r2.xyz;
  r0.yzw = r2.www * r0.yzw + r2.xyz;
  r1.xyz = sp_glow_strength * sp_colour.xyz;
  r0.yzw = r1.xyz * r0.yzw;

  // Same shape as highlightindicator02. Alpha is stock - it drives the blend.
  o0.xyz = r0.yzw * r0.xxx;
  // o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
  // if (HDR >= 0.5f) o0.xyz *=  0.5f;
  o0.xyz *= 0.5f;         //IT WAS TOO BRIGHT.
  // o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);
  o0.w = saturate(r0.x);
  
  return;
}