// ---- Created with 3Dmigoto v1.3.16 on Mon Sep 28 09:13:31 2026

// WARHAMMER 2 sharpening00 - 5-tap Laplacian on a copy of the frame.
// Black scene: the FP16 frame -> 8-bit copy was dropped. Fixed in addon (frame_copy).
// Stock saturate() clipped the frame to UI white. Now in Paper White units:
// SDR: stock. HDR: stock result on SDR-clipped taps; values outside 0..1
// (highlights, wide gamut) pass through untouched.

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

cbuffer SHARPEN_CONSTANTS : register(b1)
{
  float g_sharpening_strength : packoffset(c0);
}

Texture2D<float3> t_frame : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Stock kernel, stock order: 4*centre - up - left - right - down.
float3 SharpenLaplacian(float3 c, float3 up, float3 left, float3 right, float3 down)
{
  return (((c * 5.f + (-up - left)) - right) - down) - c;
}


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.zw = float2(0,0);
  r1.xyzw = float4(-1,-1,-1,-1) + g_render_target_dimensions.xyxy;
  r1.xyzw = (int4)r1.xyzw;
  r2.xy = (int2)v0.xy;
  r3.xyzw = (int4)r2.xyxy + int4(0,-1,-1,0);
  r3.xyzw = max(int4(0,0,0,0), (int4)r3.xyzw);
  r3.xyzw = min((int4)r3.zwxy, (int4)r1.zwxy);
  r0.xy = r3.zw;
  float3 tap_up = t_frame.Load(r0.xyz).xyz;
  r3.zw = float2(0,0);
  float3 tap_left = t_frame.Load(r3.xyz).xyz;
  r2.zw = float2(0,0);
  float3 tap_centre = t_frame.Load(r2.xyz).xyz;
  r2.xyzw = (int4)r2.xyxy + int4(1,0,0,1);
  r2.xyzw = max(int4(0,0,0,0), (int4)r2.xyzw);
  r1.xyzw = min((int4)r2.zwxy, (int4)r1.zwxy);
  r2.xy = r1.zw;
  r2.zw = float2(0,0);
  float3 tap_right = t_frame.Load(r2.xyz).xyz;
  r1.zw = float2(0,0);
  float3 tap_down = t_frame.Load(r1.xyz).xyz;

  // Paper White units: SDR white = 1 (frame is 2.2-encoded, scaled by t00).
  float pw_encoded = renodx::color::gamma::EncodeSafe(SI.diffuse_white_nits / SI.graphics_white_nits);
  float3 c = tap_centre / pw_encoded;
  float3 up = tap_up / pw_encoded;
  float3 left = tap_left / pw_encoded;
  float3 right = tap_right / pw_encoded;
  float3 down = tap_down / pw_encoded;

  float3 sharpened;
  if (HDR < 0.5f) {
    // Stock.
    sharpened = saturate(g_sharpening_strength * SharpenLaplacian(c, up, left, right, down) + c);
  } else {
    // What SDR would output for this pixel, from its clipped taps.
    float strength = g_sharpening_strength * SI.sharpening;
    float3 sdr_c = saturate(c);
    float3 sdr_sharpened = saturate(
        strength * SharpenLaplacian(sdr_c, saturate(up), saturate(left), saturate(right), saturate(down)) + sdr_c);
    // Inside 0..1: SDR's result. Outside: HDR/wide-gamut data SDR never had, kept.
    sharpened = renodx::math::Select(saturate(c) == c, sdr_sharpened, c);
  }

  o0.xyz = sharpened * pw_encoded;
  o0.w = 1;
  return;
}
