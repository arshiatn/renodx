// particle_ribbon.hlsl volumetric_ribbon_pixel_main: ribbon particles (trails), hidden behind the scene by depth.
// Render scale (DLSS addon): the CPU's g_viewport_offset_scale stays the full screen while the 3D passes draw into
// the top-left render scale part: the viewport comes from the camera instead (g_viewport_dimensions, patched to the
// render size by the DLSS addon; the game's own "respect the viewport" formula). At full size the same as the game's.
// HDR: x VFX Brightness: Trails.
#include "../shared.h"

cbuffer camera_VS_PS : register(b0)
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

cbuffer shroud_PS : register(b1)
{
  float g_shroud_enabled : packoffset(c0) = {0};
  float g_campaign_autumn_timer : packoffset(c0.y) = {0};
  float4 g_campaign_world_bounds : packoffset(c1) = {0,0,1,1};
  float4 g_campaign_playable_area_bounds : packoffset(c2) = {0,0,1,1};
  float g_snow_height : packoffset(c3) = {1.39999998};
  float g_rock_slope_scale : packoffset(c3.y) = {0.400000006};
  float g_rock_height_min : packoffset(c3.z) = {0.400000006};
  float g_rock_height_max : packoffset(c3.w) = {1.39999998};
}

cbuffer particle_ribbon_PS : register(b2)
{
  float4 g_viewport_offset_scale : packoffset(c0);
  float g_readable_depth : packoffset(c1);
}

cbuffer ribbon_scripted_effect_info_constants_PS : register(b3)
{
  float g_shroud_alpha_min_value : packoffset(c0);
}

SamplerState s_shroud_s : register(s0);
SamplerState s_texture_ribbon_s : register(s1);
Texture2D<float4> s_texture_ribbon : register(t0);
Texture2D<float4> s_shroud : register(t1);
Texture2D<float4> gbuffer_readable_depth_texture : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  float3 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = s_texture_ribbon.Sample(s_texture_ribbon_s, v3.xy).x;
  r0.yz = s_texture_ribbon.Sample(s_texture_ribbon_s, v2.xy).yz;
  r1.xy = s_texture_ribbon.Sample(s_texture_ribbon_s, v2.zw).yz;
  r0.z = r0.y + r0.z;
  r0.w = r1.x + r1.y;
  r1.x = cmp(0 != g_shroud_enabled);
  if (r1.x != 0) {
    r1.xz = -g_campaign_world_bounds.xy + v6.xz;
    r2.xy = g_campaign_world_bounds.zw + -g_campaign_world_bounds.xy;
    r1.xz = r1.xz / r2.xy;
    r1.x = s_shroud.SampleLevel(s_shroud_s, r1.xz, r1.z).x;
    r1.x = saturate(-0.5 + r1.x);
    r1.x = r1.x + r1.x;
  } else {
    r1.x = 1;
  }
  r1.x = max(g_shroud_alpha_min_value, r1.x);
  r1.x = min(1, r1.x);
  r0.x = r0.x * r0.z;
  r0.x = r0.x * r0.w;
  r0.x = v1.w * r0.x;
  r0.x = v3.z * r0.x;
  r0.x = saturate(r0.x * r1.x);
  r0.z = -0.00392156886 + r0.x;
  r0.z = cmp(r0.z < 0);
  if (r0.z != 0) discard;
  r0.z = cmp(g_readable_depth != 0.000000);
  if (r0.z != 0) {
    r0.zw = v4.xy / v4.ww;
    r0.zw = r0.zw * float2(0.5,-0.5) + float2(0.5,0.5);
    r0.zw = (g_viewport_origin + r0.zw * g_viewport_dimensions.xy) * g_render_target_dimensions.zw;
    r1.xz = float2(-1,-1) + screen_size.xy;
    r1.xz = r1.xz * r0.zw;
    r2.xy = (int2)r1.xz;
    r2.zw = float2(0,0);
    r2.z = gbuffer_readable_depth_texture.Load(r2.xyz).x;
    r2.xy = r0.zw * float2(2,-2) + float2(-1,1);
    r2.w = 1;
    r0.z = dot(r2.xyzw, inv_projection._m02_m12_m22_m32);
    r0.w = dot(r2.xyzw, inv_projection._m03_m13_m23_m33);
    r0.z = r0.z / r0.w;
    r0.w = cmp(r0.z < v5.z);
    if (r0.w != 0) discard;
    r0.z = saturate(-v5.z + r0.z);
    o0.w = r0.x * r0.z;
  } else {
    o0.w = r0.x;
  }
  r0.x = r1.y + r0.y;
  o0.xyz = v1.xyz * r0.xxx;
  if (HDR >= 1.f) o0.xyz *= SI.vfx_trails;
  return;
}