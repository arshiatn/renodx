// AA FXAA

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

cbuffer sprite_PS : register(b1)
{
  float g_windows_time_PS : packoffset(c0);
  float g_model_time_PS : packoffset(c0.y);
  float g_text_rendering_enabled_PS : packoffset(c0.z);
  float2 g_screen_dimensions_PS : packoffset(c1);
  float2 g_campaign_shroud_uv_offset : packoffset(c1.z);
  float4 g_texture_dimensions : packoffset(c2);
  float4 g_mask_and_image_dimensions : packoffset(c3);
  float4 g_mask_position_and_pivot_position : packoffset(c4);
  float4x4 g_mask_transform : packoffset(c5);
  float2 g_mask_atlas_uvs : packoffset(c9);
  float2 g_texture_atlas_size : packoffset(c9.z);
}

cbuffer constant_tone_mapping_buffer : register(b2)
{
  float g_luma_black_point : packoffset(c0);
  float g_luma_white_point : packoffset(c0.y);
  float4 g_tc_remap : packoffset(c1);
}

SamplerState s_alt_map_s : register(s0);
SamplerState g_hdr_rgb_texture_sampler_s : register(s1);
Texture2D<float4> t_alt_map : register(t0);
Texture2D<float4> g_hdr_rgb_texture : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v0.xy * g_tc_remap.xy + g_tc_remap.zw;
  r0.z = cmp(0 < g_mask_and_image_dimensions.z);
  if (r0.z != 0) {
    r1.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
    r1.zw = v0.zw;
    r2.x = dot(r1.xyzw, g_mask_transform._m00_m10_m20_m30);
    r2.y = dot(r1.xyzw, g_mask_transform._m01_m11_m21_m31);
    r0.zw = g_mask_position_and_pivot_position.zw + r2.xy;
    r0.zw = -g_mask_position_and_pivot_position.xy + r0.zw;
    r0.zw = r0.zw / g_mask_and_image_dimensions.xy;
    r1.xy = cmp(r0.zw >= float2(0,0));
    r1.zw = cmp(float2(1,1) >= r0.zw);
    r1.x = r1.z ? r1.x : 0;
    r1.x = r1.y ? r1.x : 0;
    r1.x = r1.w ? r1.x : 0;
    if (r1.x != 0) {
      r1.xy = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r0.zw = r0.zw * r1.xy + g_mask_atlas_uvs.xy;
      r0.z = t_alt_map.SampleLevel(s_alt_map_s, r0.zw, 0).w;
    } else {
      r0.z = 0;
    }
  } else {
    r0.z = 1;
  }
  r0.xy = g_vpos_texel_offset + r0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyzw = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;

  // FP16 keeps NaNs from sqrt(negative); stock normalized writes effectively hid them.
  r2.xyz = sqrt(max(r1.xyz, 0.f.xxx));

  r1.xyz = r2.xyz * r0.zzz;
  r0.w = 0.5;
  o0.xyzw = r1.xyzw * r0.wwwz;

  if (HDR >= 0.5f)
  {
    // Stock's sqrt() is gamma 2.0, but the swapchain decodes 2.2 (as the display
    // did in stock). Decoding 2.2 keeps the round trip exact below the knee.
    float3 backdrop =
        renodx::color::gamma::DecodeSafe(
            max(o0.xyz, 0.f.xxx));

    // 1.0 in this compositing buffer corresponds to UI white.
    // Allow recovered scene detail to use the actual display peak.
    float ui_white = max(SI.graphics_white_nits, 0.0001f);
    float ceiling =
        max(SI.peak_white_nits / ui_white, 1.0001f);

    // Preserve hue with a max-channel shoulder. Values <= 1.0 remain exact.
    float max_channel = max(backdrop.r, max(backdrop.g, backdrop.b));
    if (max_channel > 1.f)
    {
      float mapped =
          1.f
          + renodx::tonemap::Neutwo(
              max_channel - 1.f,
              ceiling - 1.f);

      backdrop *= mapped / max_channel;
    }

    // The finished frame is gamma-encoded, matching the swapchain decode.
    o0.xyz = renodx::color::gamma::EncodeSafe(backdrop);
    return;
  }

  // Restore the stock normalized-target RGB clamp in SDR.
  o0.xyz = saturate(o0.xyz);
  return;
}
