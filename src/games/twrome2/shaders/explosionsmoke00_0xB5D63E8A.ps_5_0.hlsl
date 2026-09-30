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
SamplerState gbuffer_channel_4_sampler_s : register(s1);
SamplerState s_diffuse1_s : register(s2);
SamplerState s_diffuse2_s : register(s3);
SamplerState s_diffuse3_s : register(s4);
SamplerState s_diffuse4_s : register(s5);
SamplerState s_diffuse5_s : register(s6);
SamplerState s_diffuse6_s : register(s7);
Texture2D<float4> s_diffuse1 : register(t0);
Texture2D<float4> s_diffuse2 : register(t1);
Texture2D<float4> s_diffuse3 : register(t2);
Texture2D<float4> s_diffuse4 : register(t3);
Texture2D<float4> s_diffuse5 : register(t4);
Texture2D<float4> s_diffuse6 : register(t5);
Texture2D<float4> gbuffer_channel_4_sampler : register(t6);
TextureCube<float4> s_sky : register(t7);


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
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(0.100000001,0.100000001) + v6.xy;
  r0.xy = floor(r0.xy);
  r0.x = (int)r0.x;
  r1.xyzw = cmp((int4)r0.xxxx == int4(5,4,3,0));
  r0.z = (int)r1.z | (int)r1.w;
  r0.z = (int)r1.y | (int)r0.z;
  r0.z = (int)r1.x | (int)r0.z;
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
            r0.w = cmp((int)r0.y == 4);
            if (r0.w != 0) {
              r2.xyzw = s_diffuse5.Sample(s_diffuse5_s, v2.xy).xyzw;
            } else {
              r0.w = cmp((int)r0.y == 5);
              if (r0.w != 0) {
                r2.xyzw = s_diffuse6.Sample(s_diffuse6_s, v2.xy).xyzw;
              } else {
                r2.xyzw = float4(0,0,0,0);
              }
            }
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
            r0.w = cmp((int)r0.y == 3);
            if (r0.w != 0) {
              r3.xyzw = s_diffuse4.Sample(s_diffuse4_s, v8.xy).xyzw;
              r3.xyzw = r3.xyzw + -r2.xyzw;
              r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
            } else {
              r0.w = cmp((int)r0.y == 4);
              if (r0.w != 0) {
                r3.xyzw = s_diffuse5.Sample(s_diffuse5_s, v8.xy).xyzw;
                r3.xyzw = r3.xyzw + -r2.xyzw;
                r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
              } else {
                r0.y = cmp((int)r0.y == 5);
                if (r0.y != 0) {
                  r3.xyzw = s_diffuse6.Sample(s_diffuse6_s, v8.xy).xyzw;
                  r3.xyzw = r3.xyzw + -r2.xyzw;
                  r2.xyzw = v8.zzzz * r3.xyzw + r2.xyzw;
                }
              }
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
  r0.yw = r0.xx ? float2(0,0) : float2(0.75,1);
  r3.xyzw = r1.zzyy ? float4(1,0.75,1,0.75) : 0;
  r0.yw = r3.yx + r0.yw;
  r0.yw = r0.yw + r3.wz;
  r3.xy = r1.xx ? float2(0.75,1) : 0;
  r0.yw = r3.xy + r0.yw;
  r1.w = dot(-sun_direction.xyz, -sun_direction.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = -sun_direction.xyz * r1.www;
  r1.w = dot(r3.xyz, -g_camera_aligned_z_axis.xyz);
  r1.w = max(0, r1.w);
  r1.w = -1 + r1.w;
  r0.y = r1.w * r0.y;
  r0.y = v4.w * r0.y + 1;
  r3.xyz = r2.xyz * r0.www;
  r4.xyz = r3.xyz * r0.yyy;
  r4.xyz = sun_colour.xyz * r4.xyz;
  r0.y = cmp(0 < g_hdr_on);
  r0.yw = r0.yy ? float2(0.00400000019,360) : float2(1,1);
  r5.xyz = ambient_cube_tb[0].xyz * r0.www;
  r3.xyz = r5.xyz * r3.xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r3.xyz = r4.xyz * r0.yyy + r3.xyz;
  r3.xyz = v4.zzz * r3.xyz;
  r3.w = r2.w;
  r2.xyzw = r0.zzzz ? r3.xyzw : r2.xyzw;
  if (r0.z != 0) {
    r0.x = r0.x ? 0 : 2;
    r1.xyz = r1.zyx ? float3(2,4,2) : 0;
    r0.x = r1.x + r0.x;
    r0.x = r0.x + r1.y;
    r0.x = r0.x + r1.z;
    r1.xyz = v3.xyz;
    r1.w = 1;
    r3.x = dot(r1.xyzw, view_projection._m00_m10_m20_m30);
    r3.y = dot(r1.xyzw, view_projection._m01_m11_m21_m31);
    r0.w = dot(r1.xyzw, view_projection._m03_m13_m23_m33);
    r3.xy = r3.xy / r0.ww;
    r3.xy = r3.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r3.xy = r3.xy * g_viewport_offset_scale.zw + g_viewport_offset_scale.xy;
    r4.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r3.xy, 0).x;
    r4.xy = r3.xy * float2(2,-2) + float2(-1,1);
    r4.w = 1;
    r0.w = dot(r4.xyzw, inv_projection._m02_m12_m22_m32);
    r3.x = dot(r4.xyzw, inv_projection._m03_m13_m23_m33);
    r0.w = r0.w / r3.x;
    r1.x = dot(r1.xyzw, view._m02_m12_m22_m32);
    r0.w = -r1.x + r0.w;
    r0.x = saturate(r0.w / r0.x);
    r2.xyzw = r2.xyzw * r0.xxxx;
  }
  r1.xyz = -camera_position.xyz + v3.xyz;
  r1.w = max(0, r1.y);
  r3.xyz = s_sky.Sample(s_sky_s, r1.xwz).xyz;
  if (r0.z != 0) {
    r0.x = dot(r1.xyz, r1.xyz);
    r0.x = sqrt(r0.x);
    r0.z = 1 + -g_fog_distance_start;
    r0.z = r0.z * 8 + -4;
    r0.w = 1 + -g_fog_distance_strength;
    r0.w = 1000 * r0.w;
    r0.w = r0.x / r0.w;
    r0.z = r0.z + r0.w;
    r0.z = 1.44269502 * r0.z;
    r0.z = exp2(r0.z);
    r0.z = g_fog_distance_scale / r0.z;
    r0.z = saturate(g_fog_distance_scale + -r0.z);
    r0.w = g_fog_height_top + -v3.y;
    r1.x = g_fog_height_top + -g_fog_height_bottom;
    r0.w = -g_fog_height_bottom + r0.w;
    r1.x = 1 / r1.x;
    r0.w = saturate(r1.x * r0.w);
    r1.x = r0.w * -2 + 3;
    r0.w = r0.w * r0.w;
    r0.w = r1.x * r0.w;
    r1.x = max(0.00100000005, g_fog_clear_distance);
    r1.x = 1 / r1.x;
    r0.x = r1.x * r0.x;
    r0.x = min(1, r0.x);
    r1.x = r0.x * -2 + 3;
    r0.x = r0.x * r0.x;
    r0.x = r1.x * r0.x;
    r1.xyz = g_volume_fog_colour.xyz * sun_colour.xyz;
    r1.xyz = float3(1.5,1.5,1.5) * r1.xyz;
    r1.xyz = abs(sun_direction.yyy) * r1.xyz;
    r4.xyz = r1.xyz * r0.yyy;
    r1.w = saturate(dot(r0.zz, g_fog_colour_blend));
    r1.xyz = -r1.xyz * r0.yyy + r3.xyz;
    r1.xyz = r1.www * r1.xyz + r4.xyz;
    r0.y = g_fog_height_strength * r0.w + r0.z;
    r0.x = saturate(r0.x * r0.y);
    r0.yzw = r1.xyz * r2.www + -r2.xyz;
    r2.xyz = r0.xxx * r0.yzw + r2.xyz;
  }
  o0.xyzw = r2.xyzw;
  o0.rgb *= VfxFireBrightness;
  o0.w = saturate(o0.w);
  return;
}