// battlefog.hlsl ps40_main_new_fog_ms (MSAA, per sample), the game's D9A3B04F: the battle's distance and
// height fog (3Dmigoto decompile, tools\tw\troy\fog\par_t_make_fog.py) with Attila's Fog Strength. Its NDC over the draw's
// share below 100% (view_share patched the game's bytes there, not this copy). The game's bits at Fog Strength 1 with
// nothing at b12: WARP test\tw_troy_shaders.
#include "../../twattila/render_scale.hlsli"

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

cbuffer fog : register(b1)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_density_mie : packoffset(c1);
  float g_fog_noise : packoffset(c1.y);
  float3 g_fog_colour : packoffset(c2);
  uint g_fog_mode : packoffset(c2.w);
  float g_fog_lerp : packoffset(c3);
  float g_fog_clear_distance : packoffset(c3.y);
  float g_fog_y_multiplier : packoffset(c3.z);
  float g_campaign_density_scaling : packoffset(c3.w);
  float g_height_fog_clear_distance : packoffset(c4);
  float g_clear_distance_fade_out : packoffset(c4.y);
  float3 g_height_fog_colour : packoffset(c5);
  float g_height_fog_falloff : packoffset(c5.w);
  float g_height_fog_clear_falloff : packoffset(c6);
}

Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  uint v1 : SV_SampleIndex0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.z = gbuffer_channel_4_texture_ms.Load(r0.xy, v1.x).x;
  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r0.xy = (r1.xy / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r0.w = 1;
  r1.x = dot(r0.xyzw, inv_view_projection._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, inv_view_projection._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.x = dot(r0.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r1.xyz / r0.xxx;
  r0.w = -g_fog_height_top + r0.y;
  r1.xz = camera_position.xz;
  r1.w = -g_fog_height_top + camera_position.y;
  r1.y = min(200, r1.w);
  r1.xzw = -r1.xyz + r0.xwz;
  r0.xyz = camera_position.xyz + -r0.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.y = max(1, r0.w);
  r0.y = 100 / r0.y;
  r0.z = -g_height_fog_falloff * r1.y;
  r0.z = 1.44269502 * r0.z;
  r0.z = exp2(r0.z);
  r0.w = dot(r1.xzw, r1.xzw);
  r0.xw = sqrt(r0.xw);
  r0.z = r0.w * r0.z;
  r0.w = g_height_fog_falloff * r1.z;
  r1.x = cmp(0.00999999978 < abs(r1.z));
  r1.y = -1.44269502 * r0.w;
  r1.y = exp2(r1.y);
  r1.y = 1 + -r1.y;
  r0.w = r1.y / r0.w;
  r0.w = r0.z * r0.w;
  r0.z = r1.x ? r0.w : r0.z;
  r0.y = r0.z * r0.y;
  r0.y = -g_fog_density_height * r0.y;
  r0.y = 1.44269502 * r0.y;
  r0.y = exp2(r0.y);
  r0.y = -1 + r0.y;
  r0.z = max(1, g_height_fog_clear_distance);
  r0.z = r0.x / r0.z;
  r0.x = -g_fog_clear_distance + r0.x;
  r0.x = max(0, r0.x);
  r0.z = min(1, r0.z);
  r0.w = log2(r0.z);
  r0.w = 0.03125 * r0.w;
  r0.w = exp2(r0.w);
  r0.w = r0.w + -r0.z;
  r0.z = g_height_fog_clear_falloff * r0.w + r0.z;
  r0.y = r0.z * r0.y + 1;
  r0.z = max(0, -g_fog_clear_distance);
  r0.x = r0.x + -r0.z;
  r0.x = g_fog_density_constant * -r0.x;
  r0.x = 0.0144269504 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.y = max(r0.y, r0.x);
  r0.z = 1 + -r0.y;
  r1.xyz = g_height_fog_colour.xyz * r0.zzz;
  r1.xyz = float3(3,3,3) * r1.xyz;
  r2.xyz = g_fog_colour.xyz * r0.xxx;
  r0.x = 1 + -r0.x;
  o0.w = min(r0.x, r0.y);
  o0.xyz = r2.xyz * float3(3,3,3) + r1.xyz;
  // Fog Strength (SI.fog_strength; 1: the game's, these bits untouched): the fog's optical depth x it. The blend adds o0.rgb
  // and keeps o0.w of what lies behind: o0.w ^ s, the in-scattered light by the same share of the fog's opacity.
  if (SI.fog_strength != 1.f) {
    const float transmittance = (SI.fog_strength > 0.f) ? exp2(SI.fog_strength * log2(max(o0.w, 1e-30f))) : 1.f;
    const float opacity = 1.f - o0.w;
    o0.xyz *= (opacity > 1e-6f) ? ((1.f - transmittance) / opacity) : SI.fog_strength;
    o0.w = transmittance;
  }
  return;
}