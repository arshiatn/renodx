// PHARAOH uisprite00 - UI sprite / text with an 8-tap outline. Same sprite_PS + mask +
// scissor layout as cinematics00, so it draws in the UI pass, after t01-t04.
//
// NOT a source of negative nits. Stock already saturates the RGB before the encode, and a
// gamma encode of [0,1] stays in [0,1] - checked over +/-50 plus inf/NaN at gamma 1.0-3.0,
// zero negatives, zero NaN. The alpha is a lerp of three [0,1] terms, so also clean.
//
// ONE change: the final encode. Stock uses the in-game Gamma slider, the proxy decodes
// 2.2, so normalise to 2.2 - exactly what t01-t04's SDR path does the long way round.
// No paper-white ratio: UI belongs at Graphics White, and 1.0 is what the proxy reads as
// Graphics White. (cinematics00 gets the ratio because a cutscene is content, not chrome.)
//
// The nine log2 x g_inv_gamma_output blocks below are LEFT ALONE. Those gamma-correct font
// coverage into an alpha, they are not a display encode; retargeting them to 2.2 would
// change text weight.

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

SamplerState s_diffuse_map_s : register(s0);
SamplerState s_mask_map_s : register(s1);
Texture2D<float4> t_diffuse_map : register(t0);
Texture2D<float4> t_mask_map : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float2 v6 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t_diffuse_map.SampleLevel(s_diffuse_map_s, v2.xy, 0).xyzw;
  r1.x = cmp(0.5 < g_text_rendering_enabled_PS);
  r1.y = saturate(r0.x);
  r1.y = log2(r1.y);
  r1.y = g_inv_gamma_output * r1.y;
  r2.w = exp2(r1.y);
  r2.xyz = float3(1,1,1);
  r0.xyzw = r1.xxxx ? r2.xyzw : r0.xyzw;
  r1.y = v1.w * r0.w;
  r2.xy = -g_texture_dimensions.zw + v2.xy;
  r2.zw = v2.yx;
  r1.zw = t_diffuse_map.SampleLevel(s_diffuse_map_s, r2.xz, 0).xw;
  r1.z = saturate(r1.z);
  r1.z = log2(r1.z);
  r1.z = g_inv_gamma_output * r1.z;
  r1.z = exp2(r1.z);
  r1.z = r1.x ? r1.z : r1.w;
  r1.w = 1 + -r0.w;
  r1.z = r1.z * r1.w + r0.w;
  r3.yz = g_texture_dimensions.wz + v2.yx;
  r3.xw = v2.yx;
  r4.xy = t_diffuse_map.SampleLevel(s_diffuse_map_s, r3.zx, 0).xw;
  r4.x = saturate(r4.x);
  r1.w = log2(r4.x);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r4.y;
  r2.z = 1 + -r1.z;
  r1.z = r1.w * r2.z + r1.z;
  r2.zw = t_diffuse_map.SampleLevel(s_diffuse_map_s, r2.wy, 0).xw;
  r2.z = saturate(r2.z);
  r1.w = log2(r2.z);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r2.w;
  r2.z = 1 + -r1.z;
  r1.z = r1.w * r2.z + r1.z;
  r2.zw = t_diffuse_map.SampleLevel(s_diffuse_map_s, r3.wy, 0).xw;
  r2.z = saturate(r2.z);
  r1.w = log2(r2.z);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r2.w;
  r2.z = 1 + -r1.z;
  r1.z = r1.w * r2.z + r1.z;
  r2.zw = t_diffuse_map.SampleLevel(s_diffuse_map_s, r2.xy, 0).xw;
  r2.z = saturate(r2.z);
  r1.w = log2(r2.z);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r2.w;
  r2.z = 1 + -r1.z;
  r1.z = r1.w * r2.z + r1.z;
  r3.xw = r2.xy;
  r2.xy = t_diffuse_map.SampleLevel(s_diffuse_map_s, r3.xy, 0).xw;
  r2.x = saturate(r2.x);
  r1.w = log2(r2.x);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r2.y;
  r2.x = 1 + -r1.z;
  r1.z = r1.w * r2.x + r1.z;
  r2.xy = t_diffuse_map.SampleLevel(s_diffuse_map_s, r3.zw, 0).xw;
  r2.x = saturate(r2.x);
  r1.w = log2(r2.x);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.w = r1.x ? r1.w : r2.y;
  r2.x = 1 + -r1.z;
  r1.z = r1.w * r2.x + r1.z;
  r2.xy = t_diffuse_map.SampleLevel(s_diffuse_map_s, r3.zy, 0).xw;
  r2.x = saturate(r2.x);
  r1.w = log2(r2.x);
  r1.w = g_inv_gamma_output * r1.w;
  r1.w = exp2(r1.w);
  r1.x = r1.x ? r1.w : r2.y;
  r1.w = 1 + -r1.z;
  r1.x = r1.x * r1.w + r1.z;
  r2.w = v4.w * v1.w;
  r2.xyz = v4.xyz;
  r2.xyzw = r2.xyzw * r1.xxxx;
  r0.xyzw = r0.xyzw * v1.xyzw + -r2.xyzw;
  r0.xyzw = r1.yyyy * r0.xyzw + r2.xyzw;
  r1.x = cmp(0 < g_mask_and_image_dimensions.z);
  if (r1.x != 0) {
    r1.xy = -g_mask_position_and_pivot_position.zw + v0.xy;
    r1.zw = v0.zw;
    r2.x = dot(r1.xyzw, g_mask_transform._m00_m10_m20_m30);
    r2.y = dot(r1.xyzw, g_mask_transform._m01_m11_m21_m31);
    r1.xy = g_mask_position_and_pivot_position.zw + r2.xy;
    r1.xy = -g_mask_position_and_pivot_position.xy + r1.xy;
    r1.xy = r1.xy / g_mask_and_image_dimensions.xy;
    r1.zw = cmp(r1.xy >= float2(0,0));
    r2.xy = cmp(float2(1,1) >= r1.xy);
    r1.z = r1.z ? r2.x : 0;
    r1.z = r1.w ? r1.z : 0;
    r1.z = r2.y ? r1.z : 0;
    if (r1.z != 0) {
      r1.zw = g_mask_and_image_dimensions.zw / g_texture_atlas_size.xy;
      r1.xy = r1.xy * r1.zw + g_mask_atlas_uvs.xy;
      r1.x = t_mask_map.SampleLevel(s_mask_map_s, r1.xy, 0).w;
    } else {
      r1.x = 0;
    }
    r0.w = r1.x * r0.w;
  }
  r1.x = -0.00392156886 + r0.w;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r1.xy = v6.xy + -v5.xw;
  r1.zw = -v6.xy + v5.zy;
  r1.xyzw = cmp(r1.xyzw < float4(0,0,0,0));
  r1.xy = (int2)r1.zw | (int2)r1.xy;
  r1.x = (int)r1.y | (int)r1.x;
  if (r1.x != 0) discard;
  // Stock's saturate, then the 2.2 encode the swapchain proxy decodes. Stock's
  // pow(1/g_gamma_output) and a decode back would cancel, so it collapses to this.
  o0.xyz = renodx::color::gamma::EncodeSafe(saturate(r0.xyz));
  o0.w = saturate(r0.w);
  return;
}