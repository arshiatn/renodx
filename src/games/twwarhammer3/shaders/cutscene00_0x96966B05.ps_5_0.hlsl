// ---- Created with 3Dmigoto v1.3.16 on Sat Sep 26 13:13:42 2026

// WARHAMMER 3 cutscene00 - video sprite (BT.601 limited-range YUV -> RGB), WH2's
// cinematic00 plus mask margins/tiling. Same fix as WH2: stock encodes
// pow(1/g_gamma_output), the proxy decodes 2.2. Encode 2.2 at Paper White, all modes.

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
  float4 r0,r1,r2,r3,r4,r5,r6;
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
    r3.z = dot(r2.xyzw, g_mask_transform._m00_m10_m20_m30);
    r3.w = dot(r2.xyzw, g_mask_transform._m01_m11_m21_m31);
    r0.xy = g_mask_position_and_pivot_position.zw + r3.zw;
    r0.xy = -g_mask_position_and_pivot_position.xy + r0.xy;
    r2.xy = r0.xy / g_mask_and_image_dimensions.xy;
    r2.zw = cmp(r2.xy >= float2(0,0));
    r3.xy = cmp(float2(1,1) >= r2.xy);
    r2.z = r2.z ? r3.x : 0;
    r2.z = r2.w ? r2.z : 0;
    r2.z = r3.y ? r2.z : 0;
    if (r2.z != 0) {
      r2.z = g_mask_pixel_margins.x + g_mask_pixel_margins.y;
      r2.z = g_mask_pixel_margins.z + r2.z;
      r2.z = g_mask_pixel_margins.w + r2.z;
      r2.z = cmp(0 < r2.z);
      r2.w = cmp(g_mask_is_tiled != 0);
      r2.z = (int)r2.w | (int)r2.z;
      if (r2.z != 0) {
        r3.xyzw = -g_mask_pixel_margins.wwxx + g_mask_and_image_dimensions.xzyw;
        r3.xyzw = -g_mask_pixel_margins.yyzz + r3.xyzw;
        r4.xyzw = cmp(float4(0,0,0,0) < g_mask_pixel_margins.wyxz);
        r4.xy = r4.yw ? r4.xz : 0;
        r5.xyzw = cmp(g_mask_pixel_margins.wyxz == float4(0,0,0,0));
        r2.z = r5.y ? r5.x : 0;
        r2.z = r5.z ? r2.z : 0;
        r2.z = r5.w ? r2.z : 0;
        r5.xy = -g_mask_pixel_margins.wx + r0.xy;
        r4.zw = cmp(r5.xy < float2(0,0));
        r6.xy = r4.zw ? r0.xy : 0;
        r6.xy = g_mask_pixel_margins.wx + r6.xy;
        r5.zw = r4.zw ? r0.xy : r6.xy;
        r6.xy = r5.xy + -r3.xz;
        r0.xy = cmp(r6.xy < float2(0,0));
        r3.xz = (int2)r2.zz | (int2)r4.xy;
        r2.zw = r2.ww ? r3.xz : 0;
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
        r3.xz = r0.xy ? r2.zw : r5.zw;
        r3.xy = r3.xz + r3.yw;
        r6.zw = r0.xy ? r2.zw : r3.xy;
        r0.xy = (int2)r4.zw | (int2)r0.xy;
        r3.xyzw = r4.zzww ? r5.xzyw : r6.xzyw;
        r2.zw = -g_mask_pixel_margins.yz + r3.xz;
        r2.zw = cmp(r2.zw < float2(0,0));
        r3.xz = r3.yw + r3.xz;
        r4.xy = r2.zw ? r3.xz : r3.yw;
        r4.xy = g_mask_pixel_margins.yz + r4.xy;
        r2.zw = r2.zw ? r3.xz : r4.xy;
        r0.xy = r0.xy ? r3.yw : r2.zw;
        r2.xy = r0.xy / g_mask_and_image_dimensions.zw;
      }
      r0.xy = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r0.xy = r2.xy * r0.xy + g_mask_atlas_uvs.xy;
      r0.x = t_mask_map.SampleLevel(s_mask_map_s, r0.xy, 0).w;
    } else {
      r0.x = 0;
    }
    r2.w = v1.w * r0.x;
  } else {
    r2.w = v1.w;
  }
  r3.xyz = cmp(float3(0.0404499993,0.0404499993,0.0404499993) >= r1.xzw);
  r1.xyzw = float4(0.0773993805,0.947867334,0.0773993805,0.0773993805) * r1.xyzw;
  r0.x = log2(abs(r1.y));
  r0.x = 2.4000001 * r0.x;
  r0.x = exp2(r0.x);
  r4.x = saturate(r3.x ? r1.x : r0.x);
  r0.xy = float2(0.584705055,-1.02666891) + r0.zw;
  r0.xy = float2(0.947867334,0.947867334) * r0.xy;
  r0.xy = log2(abs(r0.xy));
  r0.xy = float2(2.4000001,2.4000001) * r0.xy;
  r0.xy = exp2(r0.xy);
  r4.yz = saturate(r3.yz ? r1.zw : r0.xy);
  // Paper White + the 2.2 encode the proxy decodes.
  r2.xyz = renodx::color::gamma::EncodeSafe(
      r4.xyz * (SI.diffuse_white_nits / SI.graphics_white_nits));
  r0.x = -0.00392156886 + r2.w;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.xy = v5.xy + -v4.xw;
  r0.zw = -v5.xy + v4.zy;
  r0.xyzw = cmp(r0.xyzw < float4(0,0,0,0));
  r0.xy = (int2)r0.zw | (int2)r0.xy;
  r0.x = (int)r0.y | (int)r0.x;
  if (r0.x != 0) discard;
  o0.xyzw = r2.xyzw;
  return;
}