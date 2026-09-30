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

cbuffer cb_vfx : register(b2)
{
  float3 g_camera_aligned_x_axis : packoffset(c0);
  float3 g_camera_aligned_y_axis : packoffset(c1);
  float3 g_camera_aligned_z_axis : packoffset(c2);
  float4 g_viewport_offset_scale : packoffset(c3);
  float2 g_screen_scale : packoffset(c4);
  float4 g_variables[200] : packoffset(c5);
}

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState s_diffuse1_s : register(s1);
SamplerState s_diffuse2_s : register(s2);
SamplerState s_diffuse3_s : register(s3);
SamplerState s_diffuse4_s : register(s4);
Texture2D<float4> s_diffuse1 : register(t0);
Texture2D<float4> s_diffuse2 : register(t1);
Texture2D<float4> s_diffuse3 : register(t2);
Texture2D<float4> s_diffuse4 : register(t3);
Texture2D<float4> gbuffer_channel_4_sampler : register(t4);


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
  r0.zw = cmp((int2)r0.xx == int2(1,0));
  r1.x = r0.z ? 1.000000 : 0;
  r1.y = r0.x ? 0 : 1;
  r0.z = (int)r0.z | (int)r0.w;
  if (r0.z != 0) {
    r0.y = (int)r0.y;
    if (r0.y == 0) {
      r2.xyzw = s_diffuse1.Sample(s_diffuse1_s, v2.xy).xyzw;
    } else {
      r0.w = cmp((int)r0.y == 1);
      if (r0.w != 0) {
        r2.xyzw = s_diffuse2.Sample(s_diffuse2_s, v2.xy).xyzw;
      } else {
        r0.w = cmp((int)r0.y == 2);
        if (r0.w != 0) {
          r2.xyzw = s_diffuse3.Sample(s_diffuse3_s, v2.xy).xyzw;
        } else {
          r0.w = cmp((int)r0.y == 3);
          if (r0.w != 0) {
            r2.xyzw = s_diffuse4.Sample(s_diffuse4_s, v2.xy).xyzw;
          } else {
            r2.xyzw = float4(0,0,0,0);
          }
        }
      }
    }
    r0.w = cmp(v8.x != 0.000000);
    if (r0.w != 0) {
      if (r0.y == 0) {
        r3.xyzw = s_diffuse1.Sample(s_diffuse1_s, v8.xy).xyzw;
        r3.xyzw = r3.xyzw + -r2.xyzw;
        r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
      } else {
        r0.w = cmp((int)r0.y == 1);
        if (r0.w != 0) {
          r3.xyzw = s_diffuse2.Sample(s_diffuse2_s, v8.xy).xyzw;
          r3.xyzw = r3.xyzw + -r2.xyzw;
          r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
        } else {
          r0.w = cmp((int)r0.y == 2);
          if (r0.w != 0) {
            r3.xyzw = s_diffuse3.Sample(s_diffuse3_s, v8.xy).xyzw;
            r3.xyzw = r3.xyzw + -r2.xyzw;
            r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
          } else {
            r0.y = cmp((int)r0.y == 3);
            if (r0.y != 0) {
              r3.xyzw = s_diffuse4.Sample(s_diffuse4_s, v8.xy).xyzw;
              r3.xyzw = r3.xyzw + -r2.xyzw;
              r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
            }
          }
        }
      }
    }
    r2.xyzw = v1.xyzw * r2.xyzw;
    r2.xyz = v1.www * r2.xyz;
  } else {
    r2.xyzw = v1.xyzw;
  }
  r0.y = r1.y + r1.x;
  r0.w = cmp(0 != r0.y);
  r1.xyz = r2.xyz * r0.yyy;
  r3.xyz = sun_colour.xyz * r1.xyz;
  r0.y = cmp(0 < g_hdr_on);
  r4.xy = r0.yy ? float2(0.00400000019,360) : float2(1,1);
  r4.yzw = ambient_cube_tb[0].xyz * r4.yyy;
  r1.xyz = r4.yzw * r1.xyz;
  r1.xyz = r1.xyz * r4.xxx;
  r1.xyz = r3.xyz * r4.xxx + r1.xyz;
  r1.xyz = v4.zzz * r1.xyz;
  r0.y = r0.w ? 1.000000 : 0;
  r0.w = r2.w * v4.w + -r2.w;
  r1.w = r0.y * r0.w + r2.w;
  r1.xyzw = r0.zzzz ? r1.xyzw : r2.xyzw;
  if (r0.x == 0) {
    r0.xyz = v3.xyz;
    r0.w = 1;
    r2.x = dot(r0.xyzw, view_projection._m00_m10_m20_m30);
    r2.y = dot(r0.xyzw, view_projection._m01_m11_m21_m31);
    r2.z = dot(r0.xyzw, view_projection._m03_m13_m23_m33);
    r2.xy = r2.xy / r2.zz;
    r2.xy = r2.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r2.xy = r2.xy * g_viewport_offset_scale.zw + g_viewport_offset_scale.xy;
    r3.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r2.xy, 0).x;
    r3.xy = r2.xy * float2(2,-2) + float2(-1,1);
    r3.w = 1;
    r2.x = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
    r2.y = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);
    r2.x = r2.x / r2.y;
    r0.x = dot(r0.xyzw, view._m02_m12_m22_m32);
    r0.x = saturate(r2.x + -r0.x);
    o0.xyzw = r1.xyzw * r0.xxxx;
  } else {
    o0.xyzw = r1.xyzw;
  }
  o0.rgb *= VfxFireBrightness * 0.2f;
  o0.w = saturate(o0.w);
  return;
}