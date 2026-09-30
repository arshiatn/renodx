// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 18:30:00 2026

// PHARAOH chromatic aberration - reads a copy of the frame (t_frame), R/B shifted radially.
// Black scene was the copy (FP16 frame -> 8-bit texture) being dropped; fixed in addon.
// No clamp here, so HDR passes through. SI.chromaticaberration is HDR only.

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

cbuffer SHARPEN_CONSTANTS : register(b1)
{
  float g_sharpening_strength : packoffset(c0);
  float g_chromatic_aberration_strength : packoffset(c0.y);
  float g_chromatic_aberration_falloff : packoffset(c0.z);
}

SamplerState s_frame_s : register(s0);
Texture2D<float3> t_frame : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Slider scales the game's strength (HDR only). At 0 the branch is skipped.
  float ca_strength = g_chromatic_aberration_strength * ((HDR >= 0.5f) ? SI.chromaticaberration : 1.f);

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.xyz = t_frame.Load(r0.xyz).xzy;
  r1.x = cmp(0 < ca_strength);
  if (r1.x != 0) {
    r1.xy = trunc(v0.xy);
    r1.xy = float2(0.5,0.5) + r1.xy;
    r1.zw = r1.xy * g_render_target_dimensions.zw + float2(-0.5,-0.5);
    r1.zw = r1.zw + r1.zw;
    r1.z = dot(r1.zw, r1.zw);
    // Guard: 0 at the exact centre on odd resolutions; log2(0) * 0 falloff = NaN.
    r1.z = log2(max(r1.z, 1e-12f));
    r1.z = g_chromatic_aberration_falloff * r1.z;
    r1.z = exp2(r1.z);
    r1.z = ca_strength * r1.z;
    r1.z = r1.z * r1.z + 1;
    r1.z = r1.z * r1.z;
    r1.z = 1 / r1.z;
    r1.z = 1 + -r1.z;
    r2.xy = r1.xy * g_render_target_dimensions.zw + -r1.zz;
    r0.x = t_frame.SampleLevel(s_frame_s, r2.xy, 0).x;
    r1.xy = r1.xy * g_render_target_dimensions.zw + r1.zz;
    r0.y = t_frame.SampleLevel(s_frame_s, r1.xy, 0).z;
  }
  r0.w = 1;
  o0.xyzw = r0.xzyw;
  return;
}
