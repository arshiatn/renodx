// TROY menuvideo00 - menu / UI video sprite (BT.601 limited-range YUV -> RGB).
//
// Same change as Pharaoh's cinematics00. Stock encodes pow(1/g_gamma_output); encode 2.2
// instead and put SDR white at Paper White. Same in both modes - the video is display-
// referred, there is nothing to tone map. Matches t01-t08's SDR path exactly: their
// decode/encode round trip is identity here, nothing touches the value in between.
//
// Troy's chain and swapchain proxy are gamma end to end now, so this matches them.
// Still disabled: it made no visible difference in testing.

cbuffer colorimetry_VS_PS : register(b0)
{
  float g_brightness : packoffset(c0);
  float g_gamma_output : packoffset(c0.y);
  float g_inv_gamma_output : packoffset(c0.z);
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

SamplerState s_diffuse_map_y_s : register(s0);
SamplerState s_diffuse_map_u_s : register(s1);
SamplerState s_diffuse_map_v_s : register(s2);
SamplerState s_alt_map_s : register(s3);
Texture2D<float4> t_diffuse_map_y : register(t0);
Texture2D<float4> t_diffuse_map_u : register(t1);
Texture2D<float4> t_diffuse_map_v : register(t2);
Texture2D<float4> t_alt_map : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t_diffuse_map_y.SampleLevel(s_diffuse_map_y_s, v2.xy, 0).w;
  r0.y = t_diffuse_map_v.SampleLevel(s_diffuse_map_v_s, v2.xy, 0).w;
  r0.z = t_diffuse_map_u.SampleLevel(s_diffuse_map_u_s, v2.xy, 0).w;
  r1.xyzw = float4(0,0,-0.391448975,2.01782227) * r0.zzzz;
  r1.xyzw = r0.yyyy * float4(1.59579468,1.59579468,-0.813476563,0) + r1.xyzw;
  r0.xyzw = r0.xxxx * float4(1.16412354,1.16412354,1.16412354,1.16412354) + r1.xyzw;
  r1.xyzw = float4(-0.87065506,-0.815655053,0.529705048,-1.08166885) + r0.xyzw;
  r0.x = cmp(0 < g_mask_and_image_dimensions.z);
  if (r0.x != 0) {
    r2.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
    r2.zw = v0.zw;
    r0.x = dot(r2.xyzw, g_mask_transform._m00_m10_m20_m30);
    r0.y = dot(r2.xyzw, g_mask_transform._m01_m11_m21_m31);
    r0.xy = g_mask_position_and_pivot_position.zw + r0.xy;
    r0.xy = -g_mask_position_and_pivot_position.xy + r0.xy;
    r0.xy = r0.xy / g_mask_and_image_dimensions.xy;
    r2.xy = cmp(r0.xy >= float2(0,0));
    r2.zw = cmp(float2(1,1) >= r0.xy);
    r2.x = r2.z ? r2.x : 0;
    r2.x = r2.y ? r2.x : 0;
    r2.x = r2.w ? r2.x : 0;
    if (r2.x != 0) {
      r2.xy = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r0.xy = r0.xy * r2.xy + g_mask_atlas_uvs.xy;
      r0.x = t_alt_map.SampleLevel(s_alt_map_s, r0.xy, 0).w;
    } else {
      r0.x = 0;
    }
    o0.w = v1.w * r0.x;
  } else {
    o0.w = v1.w;
  }
  r2.xyz = cmp(float3(0.0404499993,0.0404499993,0.0404499993) >= r1.xzw);
  r1.xyzw = float4(0.0773993805,0.947867334,0.0773993805,0.0773993805) * r1.xyzw;
  r0.x = log2(r1.y);
  r0.x = 2.4000001 * r0.x;
  r0.x = exp2(r0.x);
  r3.x = saturate(r2.x ? r1.x : r0.x);
  r0.xy = float2(0.584705055,-1.02666891) + r0.zw;
  r0.xy = float2(0.947867334,0.947867334) * r0.xy;
  r0.xy = log2(r0.xy);
  r0.xy = float2(2.4000001,2.4000001) * r0.xy;
  r0.xy = exp2(r0.xy);
  r3.yz = saturate(r2.yz ? r1.zw : r0.xy);
  // Paper White, then the 2.2 encode the swapchain proxy expects. Stock's
  // pow(1/g_gamma_output) and a decode back would cancel, so it collapses to this.
  o0.xyz = renodx::color::gamma::EncodeSafe(
      r3.xyz * (SI.diffuse_white_nits / SI.graphics_white_nits));
  return;
}