// sprite.hlsl blurred_yuv: the blurred videos (menu backgrounds), as cutscene00.

cbuffer sprite_PS : register(b0)
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

SamplerState s_diffuse_map_y_s : register(s0);
SamplerState s_diffuse_map_u_s : register(s1);
SamplerState s_diffuse_map_v_s : register(s2);
SamplerState s_mask_map_s : register(s3);
Texture2D<float4> t_diffuse_map_y : register(t0);
Texture2D<float4> t_diffuse_map_u : register(t1);
Texture2D<float4> t_diffuse_map_v : register(t2);
Texture2D<float4> t_mask_map : register(t3);


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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0.00499999989 * v3.x;
  r0.y = cmp(0 < g_mask_and_image_dimensions.z);
  if (r0.y != 0) {
    r1.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
    r1.zw = v0.zw;
    r0.z = dot(r1.xyzw, g_mask_transform._m00_m10_m20_m30);
    r0.w = dot(r1.xyzw, g_mask_transform._m01_m11_m21_m31);
    r0.yz = g_mask_position_and_pivot_position.zw + r0.zw;
    r0.yz = -g_mask_position_and_pivot_position.xy + r0.yz;
    r1.xy = r0.yz / g_mask_and_image_dimensions.xy;
    r1.zw = cmp(r1.xy >= float2(0,0));
    r2.xy = cmp(float2(1,1) >= r1.xy);
    r0.w = r1.z ? r2.x : 0;
    r0.w = r1.w ? r0.w : 0;
    r0.w = r2.y ? r0.w : 0;
    if (r0.w != 0) {
      r0.w = g_mask_pixel_margins.x + g_mask_pixel_margins.y;
      r0.w = g_mask_pixel_margins.z + r0.w;
      r0.w = g_mask_pixel_margins.w + r0.w;
      r0.w = cmp(0 < r0.w);
      r1.z = cmp(g_mask_is_tiled != 0);
      r0.w = (int)r0.w | (int)r1.z;
      if (r0.w != 0) {
        r2.xyzw = -g_mask_pixel_margins.wwxx + g_mask_and_image_dimensions.xzyw;
        r2.xyzw = -g_mask_pixel_margins.yyzz + r2.xyzw;
        r3.xyzw = cmp(float4(0,0,0,0) < g_mask_pixel_margins.wyxz);
        r3.xy = r3.yw ? r3.xz : 0;
        r4.xyzw = cmp(g_mask_pixel_margins.wyxz == float4(0,0,0,0));
        r0.w = r4.y ? r4.x : 0;
        r0.w = r4.z ? r0.w : 0;
        r0.w = r4.w ? r0.w : 0;
        r4.xy = -g_mask_pixel_margins.wx + r0.yz;
        r3.zw = cmp(r4.xy < float2(0,0));
        r5.xy = r3.zw ? r0.yz : 0;
        r5.xy = g_mask_pixel_margins.wx + r5.xy;
        r4.zw = r3.zw ? r0.yz : r5.xy;
        r5.xy = r4.xy + -r2.xz;
        r0.yz = cmp(r5.xy < float2(0,0));
        r2.xz = (int2)r0.ww | (int2)r3.xy;
        r1.zw = r1.zz ? r2.xz : 0;
        r2.xz = r4.xy * r2.yw;
        r2.xz = cmp(r2.xz >= -r2.xz);
        r2.xz = r2.xz ? r2.yw : -r2.yw;
        r3.xy = float2(1,1) / r2.xz;
        r3.xy = r4.xy * r3.xy;
        r3.xy = frac(r3.xy);
        r2.xz = r3.xy * r2.xz;
        r3.xy = g_mask_uv_stretching_factors.xy * r4.xy;
        r1.zw = r1.zw ? r2.xz : r3.xy;
        r1.zw = r4.zw + r1.zw;
        r2.xz = r0.yz ? r1.zw : r4.zw;
        r2.xy = r2.xz + r2.yw;
        r5.zw = r0.yz ? r1.zw : r2.xy;
        r0.yz = (int2)r3.zw | (int2)r0.yz;
        r2.xyzw = r3.zzww ? r4.xzyw : r5.xzyw;
        r1.zw = -g_mask_pixel_margins.yz + r2.xz;
        r1.zw = cmp(r1.zw < float2(0,0));
        r2.xz = r2.yw + r2.xz;
        r3.xy = r1.zw ? r2.xz : r2.yw;
        r3.xy = g_mask_pixel_margins.yz + r3.xy;
        r1.zw = r1.zw ? r2.xz : r3.xy;
        r0.yz = r0.yz ? r2.yw : r1.zw;
        r1.xy = r0.yz / g_mask_and_image_dimensions.zw;
      }
      r0.yz = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r0.yz = r1.xy * r0.yz + g_mask_atlas_uvs.xy;
      r0.y = t_mask_map.SampleLevel(s_mask_map_s, r0.yz, 0).w;
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
  r0.x = -0.00392156886 + v1.w;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = v5.xy + -v4.xw;
  r0.zw = -v5.xy + v4.zy;
  r0.xyzw = cmp(r0.xyzw < float4(0,0,0,0));
  r0.xy = (int2)r0.zw | (int2)r0.xy;
  r0.x = (int)r0.y | (int)r0.x;
  if (r0.x != 0) discard;
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
