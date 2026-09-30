// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 11 19:47:47 2026

cbuffer lighting_VS_PS : register(b0)
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

cbuffer cb_vfx : register(b1)
{
  float3 g_camera_aligned_x_axis : packoffset(c0);
  float3 g_camera_aligned_y_axis : packoffset(c1);
  float3 g_camera_aligned_z_axis : packoffset(c2);
  float4 g_viewport_offset_scale : packoffset(c3);
  float2 g_screen_scale : packoffset(c4);
  float4 g_variables[200] : packoffset(c5);
}

SamplerState s_diffuse1_s : register(s0);
SamplerState s_diffuse2_s : register(s1);
SamplerState s_diffuse3_s : register(s2);
SamplerState s_diffuse4_s : register(s3);
SamplerState s_normal1_s : register(s4);
SamplerState s_normal2_s : register(s5);
SamplerState s_normal3_s : register(s6);
SamplerState s_normal4_s : register(s7);
Texture2D<float4> s_diffuse1 : register(t0);
Texture2D<float4> s_diffuse2 : register(t1);
Texture2D<float4> s_diffuse3 : register(t2);
Texture2D<float4> s_diffuse4 : register(t3);
Texture2D<float4> s_normal1 : register(t4);
Texture2D<float4> s_normal2 : register(t5);
Texture2D<float4> s_normal3 : register(t6);
Texture2D<float4> s_normal4 : register(t7);


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

  r0.xyz = float3(0.100000001,0.100000001,0.100000001) + v6.xyz;
  r0.xyz = floor(r0.xyz);
  r0.x = (int)r0.x;
  r1.xyzw = cmp((int4)r0.xxxx == int4(3,2,1,0));
  r0.w = (int)r1.z | (int)r1.w;
  r1.w = (int)r1.y | (int)r0.w;
  r1.w = (int)r1.x | (int)r1.w;
  if (r1.w != 0) {
    r0.y = (int)r0.y;
    if (r0.y == 0) {
      r2.xyzw = s_diffuse1.Sample(s_diffuse1_s, v2.xy).xyzw;
    } else {
      r3.x = cmp((int)r0.y == 1);
      if (r3.x != 0) {
        r2.xyzw = s_diffuse2.Sample(s_diffuse2_s, v2.xy).xyzw;
      } else {
        r3.x = cmp((int)r0.y == 2);
        if (r3.x != 0) {
          r2.xyzw = s_diffuse3.Sample(s_diffuse3_s, v2.xy).xyzw;
        } else {
          r0.y = cmp((int)r0.y == 3);
          if (r0.y != 0) {
            r2.xyzw = s_diffuse4.Sample(s_diffuse4_s, v2.xy).xyzw;
          } else {
            r2.xyzw = float4(0,0,0,0);
          }
        }
      }
    }
    r2.xyzw = v1.xyzw * r2.xyzw;
    r2.xyz = v1.www * r2.xyz;
  } else {
    r2.xyzw = v1.xyzw;
  }
  if (r0.w != 0) {
    r0.y = (int)r0.z;
    if (r0.y == 0) {
      r3.xyz = s_normal1.Sample(s_normal1_s, v2.xy).xyz;
    } else {
      r0.z = cmp((int)r0.y == 1);
      if (r0.z != 0) {
        r3.xyz = s_normal2.Sample(s_normal2_s, v2.xy).xyz;
      } else {
        r0.z = cmp((int)r0.y == 2);
        if (r0.z != 0) {
          r3.xyz = s_normal3.Sample(s_normal3_s, v2.xy).xyz;
        } else {
          r0.y = cmp((int)r0.y == 3);
          if (r0.y != 0) {
            r3.xyz = s_normal4.Sample(s_normal4_s, v2.xy).xyz;
          } else {
            r3.xyz = float3(0,0,0);
          }
        }
      }
    }
    r0.yzw = r3.yxz * float3(2,2,2) + float3(-1,-1,-1);
  } else {
    r0.yzw = float3(0,0,1);
  }
  r3.xy = r0.xx ? float2(0,0) : float2(0.5,1);
  r4.xyzw = r1.zzyx ? float4(1,0.5,0.5,0.5) : 0;
  r1.xy = r4.yx + r3.xy;
  r1.xy = r1.xy + r4.zz;
  r1.xy = r1.xy + r4.ww;
  r0.xy = v5.zw * -r0.yy;
  r0.xy = r0.zz * v5.xy + r0.xy;
  r3.xyz = g_camera_aligned_y_axis.xyz * r0.yyy;
  r0.xyz = -r0.xxx * g_camera_aligned_x_axis.xyz + r3.xyz;
  r0.xyz = -r0.www * g_camera_aligned_z_axis.xyz + r0.xyz;
  r0.w = dot(-sun_direction.xyz, -sun_direction.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = -sun_direction.xyz * r0.www;
  r0.x = dot(r3.xyz, r0.xyz);
  r0.x = max(0, r0.x);
  r0.x = -1 + r0.x;
  r0.x = r1.x * r0.x;
  r0.x = v4.w * r0.x + 1;
  r0.yzw = r2.xyz * r1.yyy;
  r1.xyz = r0.yzw * r0.xxx;
  r1.xyz = sun_colour.xyz * r1.xyz;
  r0.x = cmp(0 < g_hdr_on);
  r3.xy = r0.xx ? float2(0.00400000019,360) : float2(1,1);
  r3.yzw = ambient_cube_tb[0].xyz * r3.yyy;
  r0.xyz = r3.yzw * r0.yzw;
  r0.xyz = r0.xyz * r3.xxx;
  r0.xyz = r1.xyz * r3.xxx + r0.xyz;
  r0.xyz = v4.zzz * r0.xyz;
  r0.w = r2.w;
  o0.xyzw = r1.wwww ? r0.xyzw : r2.xyzw;
  o0.rgb *= VfxBloodSplashBrightness;
  o0.w = saturate(o0.w);
  return;
}