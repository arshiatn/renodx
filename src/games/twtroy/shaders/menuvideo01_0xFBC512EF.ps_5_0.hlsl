// sprite.hlsl blurred_yuv: the blurred videos (menu backgrounds), as menuvideo00.

cbuffer sprite_PS : register(b0)
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
#include "../autohdr.hlsli"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0.00499999989 * v3.x;
  r0.y = cmp(0 < g_mask_and_image_dimensions.z);
  if (r0.y != 0) {
    r1.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
    r1.zw = v0.zw;
    r2.x = dot(r1.xyzw, g_mask_transform._m00_m10_m20_m30);
    r2.y = dot(r1.xyzw, g_mask_transform._m01_m11_m21_m31);
    r0.yz = g_mask_position_and_pivot_position.zw + r2.xy;
    r0.yz = -g_mask_position_and_pivot_position.xy + r0.yz;
    r0.yz = r0.yz / g_mask_and_image_dimensions.xy;
    r1.xy = cmp(r0.yz >= float2(0,0));
    r1.zw = cmp(float2(1,1) >= r0.yz);
    r0.w = r1.z ? r1.x : 0;
    r0.w = r1.y ? r0.w : 0;
    r0.w = r1.w ? r0.w : 0;
    if (r0.w != 0) {
      r1.xy = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r0.yz = r0.yz * r1.xy + g_mask_atlas_uvs.xy;
      r0.y = t_alt_map.SampleLevel(s_alt_map_s, r0.yz, 0).w;
    } else {
      r0.y = 0;
    }
    r0.x = r0.x * r0.y;
  }
  r0.yzw = float3(0,0,0);
  r1.x = 0;
  while (true) {
    r1.y = cmp((int)r1.x >= 8);
    if (r1.y != 0) break;
    r1.y = (int)r1.x;
    r1.y = r1.y * r0.x;
    r2.xyz = r0.yzw;
    r1.z = 0;
    while (true) {
      r1.w = cmp(16 < (int)r1.z);
      if (r1.w != 0) break;
      r1.w = (int)r1.z;
      r1.w = 0.392699093 * r1.w;
      sincos(r1.w, r3.x, r4.x);
      r3.x = r3.x * r1.y;
      r3.y = r4.x * r1.y;
      r3.xy = v2.xy + r3.xy;
      r1.w = t_diffuse_map_y.SampleLevel(s_diffuse_map_y_s, r3.xy, 0).w;
      r2.w = t_diffuse_map_v.SampleLevel(s_diffuse_map_v_s, r3.xy, 0).w;
      r3.x = t_diffuse_map_u.SampleLevel(s_diffuse_map_u_s, r3.xy, 0).w;
      r3.xyz = float3(0,-0.391448975,2.01782227) * r3.xxx;
      r3.xyz = r2.www * float3(1.59579468,-0.813476563,0) + r3.xyz;
      r3.xyz = r1.www * float3(1.16412354,1.16412354,1.16412354) + r3.xyz;
      r3.xyz = r3.xyz + r2.xyz;
      r2.xyz = float3(-0.87065506,0.529705048,-1.08166885) + r3.xyz;
      r1.z = (int)r1.z + 1;
    }
    r0.yzw = r2.xyz;
    r1.x = (int)r1.x + 1;
  }
  o0.xyz = float3(0.0078125,0.0078125,0.0078125) * r0.yzw;
  o0.w = v1.w;
  // The blurred video, as the sharp one: the game's YUV result (sRGB encoded) decoded and clipped, AutoHDR (Video
  // Dithering: 1 code value of noise on the 2.2 encoded video first), at Paper White, 2.2 encoded for the proxy.
  float3 video = saturate(renodx::color::srgb::DecodeSafe(o0.xyz));
  if (HDR >= 1.f && SI.video_autohdr > 0.f && SI.video_dithering != 0.f) {
    video = renodx::color::gamma::DecodeSafe(renodx::color::gamma::EncodeSafe(video)
                                             + (frac(52.9829189f * frac(dot(floor(v0.xy), float2(0.06711056f, 0.00583715f)))) - 0.5f) / 255.f);
  }
  o0.xyz = renodx::color::gamma::EncodeSafe(AutoHdrHighlights(saturate(video), SI.video_autohdr)
                                            * (SI.diffuse_white_nits / SI.graphics_white_nits));
  return;
}
