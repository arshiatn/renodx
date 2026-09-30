// PHARAOH highlightindicator00 - unit selection indicator (the yellow marker under a
// selected unit). Draws AFTER t00, before t01, so it writes into the post-process
// target - the one the addon upgrades from UNORM to FP16.
//
// Stock ends with a hard 10x overbright. On the UNORM target the ROP clamped that to
// 1.0; on the upgraded target it survives, sails through t01's bridge and pins to
// whatever peak_white_nits is set to. That happens in SDR as well, since the upgrade
// applies either way - so the saturate is unconditional and SI.someindicators is not.
//
// 1.0 on the slider is exactly stock. The buffer domain differs between HDR modes:
// mode 1 is display-referred (1.0 = SDR white), mode 2 scene-linear (0.18 = mid grey),
// so the same slider value reads brighter in mode 2.

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

SamplerState indicator_texture_sampler_s : register(s0);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float4> g_indicator_texture : register(t2);
Texture2D<uint2> t_stencil : register(t3);
Texture2DMS<uint2> t_stencil_ms : register(t4);


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

  r0.xy = g_viewport_dimensions.xy * v0.xy;
  r0.zw = g_render_target_dimensions.zw * r0.xy;
  r0.xy = r0.xy * g_render_target_dimensions.zw + g_vpos_texel_offset;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.x = cmp(1 < g_num_of_samples);
  if (r1.x != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r1.yz = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r1.yz = uiDest.xy;
  }
  r2.xy = (uint2)r1.yz;
  r2.xy = r2.xy * r0.xy;
  r2.xy = floor(r2.xy);
  r2.xy = (int2)r2.xy;
  r1.yz = (int2)r1.yz + int2(-1,-1);
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r2.xy = min((int2)r2.xy, (int2)r1.yz);
  if (r1.x != 0) {
    r2.z = 0;
    r3.z = gbuffer_channel_4_texture_ms.Load(r2.xy, 0).x;
  } else {
    r2.w = 0;
    r3.z = gbuffer_channel_4_texture.Load(r2.xyw).x;
  }
  r2.xy = (int2)r0.zw;
  r2.zw = float2(0,0);
  r0.z = t_stencil_ms.Load(r2.xy, 0).y;
  r0.w = t_stencil.Load(r2.xyz).y;
  r0.z = r1.x ? r0.z : r0.w;
  r0.z = (int)r0.z & 24;
  if (r0.z == 0) discard;
  r3.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r3.w = 1;
  r0.x = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
  r0.y = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);

  // Perspective divides. Guarded so a degenerate w cannot push a NaN into the alpha.
  r0.x = renodx::math::DivideSafe(r0.x, r0.y, 0.f);
  r0.y = renodx::math::DivideSafe(v4.z, v4.w, 0.f);

  r0.z = cmp(r0.x < r0.y);
  r0.w = 1.10000002 + -abs(view._m12);
  r0.w = 5 * r0.w;
  r0.y = r0.y + -r0.x;
  r0.w = cmp(r0.y < r0.w);
  r1.x = cmp(r0.y < 5);
  r1.x = r1.x ? 1 : 0.0700000003;
  r0.w = r0.w ? r1.x : 0;
  r0.z = r0.z ? r0.w : 1;
  r1.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r2.xyzw = v3.xyzw * r1.xyzw;
  r0.y = 0.100000001 + r0.y;
  r0.y = saturate(r0.y + r0.y);
  r0.x = saturate(10 + -r0.x);
  r0.x = r0.x * -r0.y + 1;
  r0.y = r1.w * v3.w + -0.00392156886;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;

  // saturate() restores the write clamp in BOTH modes - the target is FP16 in SDR too,
  // so without it stock's overbright reads thousands of nits there as well. The slider
  // stays HDR-only.
  o0.xyz = saturate(float3(10, 10, 10) * r2.xyz);
  
  o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
  if (HDR >= 0.5f) o0.xyz *= SI.someindicators;
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);

  r0.x = r0.z * r0.x;
  o0.w = saturate(r2.w * r0.x);
  return;
}
