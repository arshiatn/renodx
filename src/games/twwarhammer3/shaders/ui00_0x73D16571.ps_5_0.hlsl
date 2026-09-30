// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 27 23:26:11 2026

// WARHAMMER 3 ui00 - UI element showing the scene through a mask (stock Reinhard + sRGB).
// Went below 0 nits: negative scene values and the scene buffer's alpha were clamped
// by the 8-bit target in SDR, not by the FP16 one. saturate restores that clamp.

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

cbuffer sprite_PS : register(b1)
{
  float g_windows_time_PS : packoffset(c0);
  float g_model_time_PS : packoffset(c0.y);
  float g_text_rendering_enabled_PS : packoffset(c0.z);
  bool g_mask_is_tiled : packoffset(c0.w);
  float2 g_screen_dimensions_PS : packoffset(c1);
  float2 g_campaign_shroud_uv_offset : packoffset(c1.z);
  float4 g_texture_dimensions : packoffset(c2);
  float4 g_mask_and_image_dimensions : packoffset(c3);
  float4 g_mask_position_and_pivot_position : packoffset(c4);
  float4x4 g_mask_transform : packoffset(c5);
  float2 g_mask_atlas_uvs : packoffset(c9);
  float2 g_texture_atlas_size : packoffset(c9.z);
  float4 g_mask_pixel_margins : packoffset(c10);
  float2 g_mask_uv_stretching_factors : packoffset(c11);
}

cbuffer tone_mapping : register(b2)
{
  float g_tone_mapping_brightness : packoffset(c0);
  float g_tone_mapping_burn : packoffset(c0.y);
  int g_use_auto_exposure : packoffset(c0.z);
}

SamplerState s_mask_map_s : register(s0);
SamplerState g_hdr_rgb_texture_sampler_s : register(s1);
SamplerState g_hdr_rgb_bloom_texture_sampler_s : register(s2);
Texture2D<float4> t_mask_map : register(t0);
Texture2D<float4> g_hdr_rgb_texture : register(t1);
Texture2D<float4> g_hdr_rgb_bloom_texture : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_screen_size.zw * v0.xy;
  r0.zw = -g_viewport_origin.xy + v0.xy;
  r0.zw = g_vpos_texel_offset + r0.zw;
  r0.zw = g_viewport_dimensions.zw * r0.zw;
  r1.xyzw = g_hdr_rgb_texture.SampleLevel(g_hdr_rgb_texture_sampler_s, r0.xy, 0).xyzw;
  r0.xyz = g_hdr_rgb_bloom_texture.SampleLevel(g_hdr_rgb_bloom_texture_sampler_s, r0.zw, 0).xyz;
  r0.xyz = r1.xyz + r0.xyz;
  r0.w = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = cmp(r0.w == 0.000000);
  r2.y = g_tone_mapping_brightness * g_tone_mapping_brightness;
  r2.z = r2.y * r0.w;
  r2.w = 1 / g_tone_mapping_burn;
  r2.w = -0.999000013 + r2.w;
  r2.w = r2.z / r2.w;
  r2.w = 1 + r2.w;
  r2.z = r2.z * r2.w;
  r2.y = r2.y * r0.w + 1;
  r2.y = r2.z / r2.y;
  r0.xyz = r2.yyy * r0.xyz;
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = r2.xxx ? float3(0,0,0) : r0.xyz;
  r2.xyz = cmp(float3(0.00313080009,0.00313080009,0.00313080009) >= r0.xyz);
  r3.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
  r0.xyz = log2(abs(r0.xyz));
  r0.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
  r0.xyz = exp2(r0.xyz);
  r0.xyz = r0.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
  r1.xyz = r2.xyz ? r3.xyz : r0.xyz;
  r0.x = cmp(0 < g_mask_and_image_dimensions.z);
  r2.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
  r2.zw = v0.zw;
  r0.z = dot(r2.xyzw, g_mask_transform._m00_m10_m20_m30);
  r0.w = dot(r2.xyzw, g_mask_transform._m01_m11_m21_m31);
  r0.yz = g_mask_position_and_pivot_position.zw + r0.zw;
  r0.yz = -g_mask_position_and_pivot_position.xy + r0.yz;
  r2.xy = r0.yz / g_mask_and_image_dimensions.xy;
  r2.zw = cmp(r2.xy >= float2(0,0));
  r3.xy = cmp(float2(1,1) >= r2.xy);
  r0.w = r2.z ? r3.x : 0;
  r0.w = r2.w ? r0.w : 0;
  r0.w = r3.y ? r0.w : 0;
  if (r0.w != 0) {
    r0.w = g_mask_pixel_margins.x + g_mask_pixel_margins.y;
    r0.w = g_mask_pixel_margins.z + r0.w;
    r0.w = g_mask_pixel_margins.w + r0.w;
    r0.w = cmp(0 < r0.w);
    r2.z = cmp(g_mask_is_tiled != 0);
    r0.w = (int)r0.w | (int)r2.z;
    if (r0.w != 0) {
      r3.xyzw = -g_mask_pixel_margins.wwxx + g_mask_and_image_dimensions.xzyw;
      r3.xyzw = -g_mask_pixel_margins.yyzz + r3.xyzw;
      r4.xyzw = cmp(float4(0,0,0,0) < g_mask_pixel_margins.wyxz);
      r4.xy = r4.yw ? r4.xz : 0;
      r5.xyzw = cmp(g_mask_pixel_margins.wyxz == float4(0,0,0,0));
      r0.w = r5.y ? r5.x : 0;
      r0.w = r5.z ? r0.w : 0;
      r0.w = r5.w ? r0.w : 0;
      r5.xy = -g_mask_pixel_margins.wx + r0.yz;
      r4.zw = cmp(r5.xy < float2(0,0));
      r6.xy = r4.zw ? r0.yz : 0;
      r6.xy = g_mask_pixel_margins.wx + r6.xy;
      r5.zw = r4.zw ? r0.yz : r6.xy;
      r6.xy = r5.xy + -r3.xz;
      r0.yz = cmp(r6.xy < float2(0,0));
      r3.xz = (int2)r0.ww | (int2)r4.xy;
      r2.zw = r2.zz ? r3.xz : 0;
      r3.xz = r5.xy * r3.yw;
      r3.xz = cmp(r3.xz >= -r3.xz);
      r3.xz = r3.xz ? r3.yw : -r3.yw;
      r4.xy = float2(1,1) / r3.xz;
      r4.xy = r5.xy * r4.xy;
      r4.xy = frac(r4.xy);
      r3.xz = r4.xy * r3.xz;
      r4.xy = g_mask_uv_stretching_factors.xy * r5.xy;
      r2.zw = r2.zw ? r3.xz : r4.xy;
      r2.zw = r5.zw + r2.zw;
      r3.xz = r0.yz ? r2.zw : r5.zw;
      r3.xy = r3.xz + r3.yw;
      r6.zw = r0.yz ? r2.zw : r3.xy;
      r0.yz = (int2)r4.zw | (int2)r0.yz;
      r3.xyzw = r4.zzww ? r5.xzyw : r6.xzyw;
      r2.zw = -g_mask_pixel_margins.yz + r3.xz;
      r2.zw = cmp(r2.zw < float2(0,0));
      r3.xz = r3.yw + r3.xz;
      r4.xy = r2.zw ? r3.xz : r3.yw;
      r4.xy = g_mask_pixel_margins.yz + r4.xy;
      r2.zw = r2.zw ? r3.xz : r4.xy;
      r0.yz = r0.yz ? r3.yw : r2.zw;
      r2.xy = r0.yz / g_mask_and_image_dimensions.zw;
    }
    r0.yz = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
    r0.yz = r2.xy * r0.yz + g_mask_atlas_uvs.xy;
    r0.y = t_mask_map.SampleLevel(s_mask_map_s, r0.yz, 0).w;
  } else {
    r0.y = 0;
  }
  r0.x = r0.x ? r0.y : 1;
  // 8-bit target clamp (rgb and alpha).
  o0.xyzw = saturate(r1.xyzw * r0.xxxx);
  return;
}