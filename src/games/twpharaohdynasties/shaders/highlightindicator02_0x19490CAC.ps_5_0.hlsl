// PHARAOH highlightindicator02 - grid / spline overlay with a distance fade.
// Alpha is the diffuse alpha times a linear fade over ~2000 units from the camera.
//
// Stock ends with a hard 20x on g_spline_colour - the biggest of the three indicators.
// Nothing clamps it once the target is FP16, in SDR as well as HDR, so the saturate is
// unconditional and SI.someindicators is not. 1.0 on the slider is exactly stock.

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

cbuffer grid_spline_PS : register(b1)
{
  float4 g_spline_colour : packoffset(c0);
  float3 g_mouse_intersected_position : packoffset(c1);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);

// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = camera_position.xz + -v2.xz;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = sqrt(r0.x);
  r0.x = -r0.x * 0.000500000024 + 1;
  r0.x = max(0, r0.x);
  r0.y = t_diffuse.Sample(s_diffuse_s, v1.xy).w;
  o0.w = saturate(r0.y * r0.x);

  // saturate() restores the write clamp in BOTH modes - the target is FP16 in SDR too,
  // so without it stock's 20x reads thousands of nits there as well. The slider stays
  // HDR-only.
  o0.xyz = saturate(float3(20,20,20) * g_spline_colour.xyz);

  o0.xyz = renodx::color::gamma::DecodeSafe(o0.xyz);
  if (HDR >= 0.5f) o0.xyz *= SI.someindicators;
  o0.xyz = renodx::color::gamma::EncodeSafe(o0.xyz);

  return;
}
