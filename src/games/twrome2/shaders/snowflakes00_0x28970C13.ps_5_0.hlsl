// ---- Created with 3Dmigoto v1.3.16 on Fri Sep 11 20:10:19 2026

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
Texture2D<float4> s_diffuse1 : register(t0);
Texture2D<float4> s_diffuse2 : register(t1);


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
  r1.xy = r0.zz ? float2(1,0.25) : 0;
  r1.zw = r0.xx ? float2(0,0) : float2(1,0.25);
  r0.x = (int)r0.z | (int)r0.w;
  if (r0.x != 0) {
    r0.y = (int)r0.y;
    if (r0.y == 0) {
      r2.xyzw = s_diffuse1.Sample(s_diffuse1_s, v2.xy).xyzw;
    } else {
      r0.y = cmp((int)r0.y == 1);
      if (r0.y != 0) {
        r2.xyzw = s_diffuse2.Sample(s_diffuse2_s, v2.xy).xyzw;
      } else {
        r2.xyzw = float4(0,0,0,0);
      }
    }
    r2.xyzw = v1.xyzw * r2.xyzw;
    r2.xyz = v1.www * r2.xyz;
  } else {
    r2.xyzw = v1.xyzw;
  }
  r0.yz = r1.zw + r1.xy;
  r0.w = cmp(0 != r0.y);
  r1.x = dot(-sun_direction.xyz, -sun_direction.xyz);
  r1.x = rsqrt(r1.x);
  r1.xyz = -sun_direction.xyz * r1.xxx;
  r1.x = dot(r1.xyz, -g_camera_aligned_z_axis.xyz);
  r1.x = max(0, r1.x);
  r1.x = -1 + r1.x;
  r0.y = r1.x * r0.y;
  r0.y = v4.w * r0.y + 1;
  r1.xyz = r2.xyz * r0.zzz;
  r3.xyz = r1.xyz * r0.yyy;
  r3.xyz = sun_colour.xyz * r3.xyz;
  r0.y = cmp(0 < g_hdr_on);
  r0.yz = r0.yy ? float2(0.00400000019,360) : float2(1,1);
  r4.xyz = ambient_cube_tb[0].xyz * r0.zzz;
  r1.xyz = r4.xyz * r1.xyz;
  r1.xyz = r1.xyz * r0.yyy;
  r1.xyz = r3.xyz * r0.yyy + r1.xyz;
  r1.xyz = v4.zzz * r1.xyz;
  r0.y = r0.w ? 1.000000 : 0;
  r0.z = r2.w * v4.w + -r2.w;
  r1.w = r0.y * r0.z + r2.w;
  o0.xyzw = r0.xxxx ? r1.xyzw : r2.xyzw;

  o0.rgb *= VfxSnowBrightness;
  o0.w = saturate(o0.w);
  return;
}