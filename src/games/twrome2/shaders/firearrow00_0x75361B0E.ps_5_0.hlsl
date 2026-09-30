// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 11 18:23:58 2026

cbuffer camera_VS_PS : register(b0)
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
  float4 g_camera_temp0 : packoffset(c30);
  float4 g_camera_temp1 : packoffset(c31);
  float4 g_camera_temp2 : packoffset(c32);
  float4 g_clip_rect : packoffset(c33);
  float g_hide_foliage : packoffset(c34);
}

cbuffer lighting_VS_PS : register(b1)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
}

cbuffer fog_VS_PS : register(b2)
{
  float3 g_volume_fog_colour : packoffset(c0);
  float g_fog_distance_start : packoffset(c0.w);
  float g_fog_distance_strength : packoffset(c1);
  float g_fog_distance_scale : packoffset(c1.y);
  float g_fog_height_bottom : packoffset(c1.z);
  float g_fog_height_top : packoffset(c1.w);
  float g_fog_height_strength : packoffset(c2);
  float g_fog_colour_blend : packoffset(c2.y);
  float g_fog_clear_distance : packoffset(c2.z);
}

cbuffer cb_vfx : register(b3)
{
  float3 g_camera_aligned_x_axis : packoffset(c0);
  float3 g_camera_aligned_y_axis : packoffset(c1);
  float3 g_camera_aligned_z_axis : packoffset(c2);
  float4 g_viewport_offset_scale : packoffset(c3);
  float2 g_screen_scale : packoffset(c4);
  float4 g_variables[200] : packoffset(c5);
}

SamplerState s_sky_s : register(s0);
SamplerState s_diffuse1_s : register(s1);
SamplerState s_diffuse2_s : register(s2);
Texture2D<float4> s_diffuse1 : register(t0);
Texture2D<float4> s_diffuse2 : register(t1);
TextureCube<float4> s_sky : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD5,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD6,
  float3 v8 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(0.100000001,0.100000001) + v6.xy;
  r0.xy = floor(r0.xy);
  r0.x = (int)r0.x;
  if (r0.x == 0) {
    r0.y = (int)r0.y;
    if (r0.y == 0) {
      r1.xyzw = s_diffuse1.Sample(s_diffuse1_s, v2.xy).xyzw;
    } else {
      r0.y = cmp((int)r0.y == 1);
      if (r0.y != 0) {
        r1.xyzw = s_diffuse2.Sample(s_diffuse2_s, v2.xy).xyzw;
      } else {
        r1.xyzw = float4(0,0,0,0);
      }
    }
    r1.xyzw = v1.xyzw * r1.xyzw;
    r1.xyz = v1.www * r1.xyz;
  } else {
    r1.xyzw = v1.xyzw;
  }
  r0.y = dot(-sun_direction.xyz, -sun_direction.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = -sun_direction.xyz * r0.yyy;
  r0.y = dot(r0.yzw, -g_camera_aligned_z_axis.xyz);
  r0.y = max(0, r0.y);
  r0.y = -1 + r0.y;
  r0.y = v4.w * r0.y;
  r0.y = r0.y * 0.75 + 1;
  r0.yzw = r1.xyz * r0.yyy;
  r0.yzw = sun_colour.xyz * r0.yzw;
  r2.x = cmp(0 < g_hdr_on);
  r2.xy = r2.xx ? float2(0.00400000019,360) : float2(1,1);
  r2.yzw = ambient_cube_tb[0].xyz * r2.yyy;
  r2.yzw = r2.yzw * r1.xyz;
  r2.yzw = r2.yzw * r2.xxx;
  r0.yzw = r0.yzw * r2.xxx + r2.yzw;
  r3.xyz = v4.zzz * r0.yzw;
  r3.w = r1.w;
  r1.xyzw = r0.xxxx ? r1.xyzw : r3.xyzw;
  r3.xyz = -camera_position.xyz + v3.xyz;
  r3.w = max(0, r3.y);
  r0.yzw = s_sky.Sample(s_sky_s, r3.xwz).xyz;
  if (r0.x == 0) {
    r0.x = dot(r3.xyz, r3.xyz);
    r0.x = sqrt(r0.x);
    r2.y = 1 + -g_fog_distance_start;
    r2.y = r2.y * 8 + -4;
    r2.z = 1 + -g_fog_distance_strength;
    r2.z = 1000 * r2.z;
    r2.z = r0.x / r2.z;
    r2.y = r2.y + r2.z;
    r2.y = 1.44269502 * r2.y;
    r2.y = exp2(r2.y);
    r2.y = g_fog_distance_scale / r2.y;
    r2.y = saturate(g_fog_distance_scale + -r2.y);
    r2.z = g_fog_height_top + -v3.y;
    r2.w = g_fog_height_top + -g_fog_height_bottom;
    r2.z = -g_fog_height_bottom + r2.z;
    r2.w = 1 / r2.w;
    r2.z = saturate(r2.z * r2.w);
    r2.w = r2.z * -2 + 3;
    r2.z = r2.z * r2.z;
    r2.z = r2.w * r2.z;
    r2.w = max(0.00100000005, g_fog_clear_distance);
    r2.w = 1 / r2.w;
    r0.x = r2.w * r0.x;
    r0.x = min(1, r0.x);
    r2.w = r0.x * -2 + 3;
    r0.x = r0.x * r0.x;
    r0.x = r2.w * r0.x;
    r3.xyz = g_volume_fog_colour.xyz * sun_colour.xyz;
    r3.xyz = float3(1.5,1.5,1.5) * r3.xyz;
    r3.xyz = abs(sun_direction.yyy) * r3.xyz;
    r4.xyz = r3.xyz * r2.xxx;
    r2.w = saturate(dot(r2.yy, g_fog_colour_blend));
    r0.yzw = -r3.xyz * r2.xxx + r0.yzw;
    r0.yzw = r2.www * r0.yzw + r4.xyz;
    r2.x = g_fog_height_strength * r2.z + r2.y;
    r0.x = saturate(r2.x * r0.x);
    r0.yzw = r0.yzw * r1.www + -r1.xyz;
    r1.xyz = r0.xxx * r0.yzw + r1.xyz;
  }
  o0.xyzw = r1.xyzw;
  o0.rgb *= VfxFireBrightness;
  o0.w = saturate(o0.w);
  return;
}