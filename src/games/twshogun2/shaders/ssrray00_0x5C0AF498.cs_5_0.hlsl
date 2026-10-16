// environment_specular_light.hlsl cs50_raycast: the reflection rays through the HiZ, one per SSR pixel (2x2 render
// pixels in the game, SSRFactor() with SSR Resolution Full: every "2" of the half resolution is SSRFactor(); ssr.hlsli). Rays
// stop at the rendered part (the game: at the buffer).

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

SamplerState s_ibl_brdf_lut_s : register(s0);
Texture2D<float4> t_ibl_brdf_lut : register(t0);
Texture2D<float4> gbuffer_normal_texture : register(t1);
Texture2D<float4> gbuffer_colour_texture : register(t2);
Texture2D<float4> gbuffer_constants_texture : register(t3);
Texture2D<float4> t_hierarchical_depth : register(t4);
RWTexture2D<float2> t_rw_hit : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "./ssr.hlsli"


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)vThreadID.xy;
  r1.xy = float2(0.5,0.5) + r0.xy;
  r1.zw = g_viewport_dimensions.xy / SSRFactor();
  r2.xy = (uint2)floor(vThreadID.xy * SSRFactor());
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r2.zw = uiDest.xy;
  r3.xy = (uint2)r2.zw;
  r2.zw = (int2)r2.zw + int2(-1,-1);
  r4.zw = float2(0,0);
  r7.xyz = float3(0,0,0);
  r0.w = 0;
  r8.z = 0;
  r3.zw = float2(0,0);
  r5.xyzw = float4(0,0,0,0);
  r6.xyzw = float4(0,0,0,0);
  r9.x = 0;
  while (true) {
    r7.w = cmp((int)r9.x >= (int)ceil(SSRFactor()));
    if (r7.w != 0) break;
    r10.xyz = r5.xyz;
    r11.xyz = r6.xyz;
    r12.xyz = r7.xyz;
    r7.w = r0.w;
    r8.x = r3.z;
    r13.x = r8.z;
    r13.y = r3.w;
    r13.z = r5.w;
    r8.w = r6.w;
    r9.y = 0;
    while (true) {
      r9.z = cmp((int)r9.y >= (int)ceil(SSRFactor()));
      if (r9.z != 0) break;
      r9.zw = (int2)r2.xy + (int2)r9.xy;
      r9.zw = (int2)r9.zw;
      r9.zw = screen_size.zw * r9.zw;
      r9.zw = r9.zw * r3.xy;
      r9.zw = floor(r9.zw);
      r9.zw = (int2)r9.zw;
      r9.zw = max(int2(0,0), (int2)r9.zw);
      r4.xy = min((int2)r9.zw, (int2)r2.zw);
      r9.zw = gbuffer_normal_texture.Load(r4.xyw).xy;
      r14.xyz = gbuffer_colour_texture.Load(r4.xyw).xyz;
      r15.xyz = gbuffer_constants_texture.Load(r4.xyz).xyw;
      r4.xy = r9.zw * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
      sincos(r4.xy, r4.xy, r16.xy);
      r17.x = r16.y;
      r17.z = r4.y;
      r16.xz = r17.xz * r16.xx;
      r4.y = 255 * r15.z;
      r4.y = round(r4.y);
      r7.w = (uint)r4.y;
      r14.xyz = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r14.xyz;
      r14.xyz = r15.xxx * r14.xyz + r11.xyz;
      r4.y = saturate(1 + -r15.y);
      r8.x = r8.x + r4.y;
      r16.y = r4.x;
      r10.xyz = r16.xyz + r10.xyz;
      r11.xyz = float3(0.0399999991,0.0399999991,0.0399999991) + r14.xyz;
      r8.w = 1 + r8.w;
      r4.x = (int)r7.w & 64;
      r14.y = r13.y + r4.y;
      r15.yzw = r16.xyz + r12.xyz;
      r14.x = r15.x + r13.x;
      r14.z = 1 + r13.z;
      r12.xyz = r4.xxx ? r15.yzw : r12.xyz;
      r13.xyz = r4.xxx ? r14.xyz : r13.xyz;
      r9.y = (int)r9.y + 1;
    }
    r5.xyz = r10.xyz;
    r6.xyz = r11.xyz;
    r7.xyz = r12.xyz;
    r0.w = r7.w;
    r3.z = r8.x;
    r8.z = r13.x;
    r3.w = r13.y;
    r5.w = r13.z;
    r6.w = r8.w;
    r9.x = (int)r9.x + 1;
  }
  r2.x = cmp(9.99999975e-005 >= r5.w);
  r2.yzw = r5.xyz / r6.www;
  r3.x = dot(r2.yzw, r2.yzw);
  r3.x = rsqrt(r3.x);
  r2.yzw = r3.xxx * r2.yzw;
  r8.y = r3.z / r6.w;
  r3.xyz = r7.xyz / r5.www;
  r4.x = dot(r3.xyz, r3.xyz);
  r4.x = rsqrt(r4.x);
  r3.xyz = r4.xxx * r3.xyz;
  r4.y = r3.w / r5.w;
  r4.z = r8.z / r5.w;
  r2.yzw = r2.xxx ? r2.yzw : r3.xyz;
  r3.yz = r2.xx ? r8.yz : r4.yz;
  r4.xy = (int2)r1.xy;
  r4.zw = float2(0,0);
  r4.z = t_hierarchical_depth.Load(r4.xyz).x;
  r5.xy = g_viewport_dimensions.zw * r1.xy;
  r4.xy = r5.xy * (2.f * SSRFactor()) * float2(1,-1) + float2(-1,1);
  r4.w = 1;
  r5.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r5.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r5.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r3.w = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r5.xyz = r5.xyz / r3.www;
  r5.w = 1;
  r7.x = dot(r5.xyzw, view._m00_m10_m20_m30);
  r7.y = dot(r5.xyzw, view._m01_m11_m21_m31);
  r7.z = dot(r5.xyzw, view._m02_m12_m22_m32);
  r4.xyw = -camera_position.xyz + r5.xyz;
  r3.w = dot(r4.xyw, r4.xyw);
  r3.w = rsqrt(r3.w);
  r4.xyw = r4.xyw * r3.www;
  r3.w = dot(r4.xyw, r2.yzw);
  r3.w = r3.w + r3.w;
  r5.xyz = r2.yzw * -r3.www + r4.xyw;
  r8.x = dot(r5.xyz, view._m00_m10_m20);
  r8.y = dot(r5.xyz, view._m01_m11_m21);
  r8.z = dot(r5.xyz, view._m02_m12_m22);
  r3.w = cmp(0 < r8.z);
  r5.xy = camera_near_far.yx + -r7.zz;
  r5.xy = r5.xy / r8.zz;
  r3.w = r3.w ? r5.x : r5.y;
  r5.xyz = r8.xyz * r3.www + r7.xyz;
  r5.w = 1;
  r7.x = dot(r5.xyzw, projection._m00_m10_m20_m30);
  r7.y = dot(r5.xyzw, projection._m01_m11_m21_m31);
  r7.z = dot(r5.xyzw, projection._m02_m12_m22_m32);
  r3.w = dot(r5.xyzw, projection._m03_m13_m23_m33);
  r5.xyz = r7.xyz / r3.www;
  r5.xyz = r5.xyz * float3(0.5,-0.5,1) + float3(0.5,0.5,0);
  r7.yz = r5.xy * r1.zw;
  r3.w = cmp(r3.z >= 9.99999975e-005);
  if (r3.w != 0) {
    r8.xyz = r3.zzz;
  } else {
    r6.xyz = r6.xyz / r6.www;
    r6.xyz = r2.xxx ? r6.xyz : float3(1,1,1);
    r2.x = 1 + -r3.y;
    r3.z = dot(r2.yzw, -r4.xyw);
    r3.x = max(0, r3.z);
    r3.z = min(1, r3.x);
    r3.w = r3.z * -5.55472994 + -6.98316002;
    r3.z = r3.w * r3.z;
    r3.z = exp2(r3.z);
    r2.x = 1 + -r2.x;
    r9.xyz = max(r2.xxx, r6.xyz);
    r9.xyz = r9.xyz + -r6.xyz;
    r6.xyz = r9.xyz * r3.zzz + r6.xyz;
    r3.yz = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r3.xy, 0).xy;
    r2.x = (int)r0.w & 128;
    r2.x = r2.x ? 1 : 0;
    r3.x = 1 + -r3.x;
    r2.x = r3.x * r2.x;
    r2.x = r2.x * -r3.y + r3.y;
    r8.xyz = r6.xyz * r2.xxx + r3.zzz;
  }
  r0.w = (int)r0.w & 65;
  r0.w = cmp(0 < (uint)r0.w);
  r2.x = dot(r8.xyz, r8.xyz);
  r2.x = cmp(0.00200000009 < r2.x);
  r0.w = r0.w ? r2.x : 0;
  r1.zw = -r5.xy * r1.zw + r0.xy;
  r1.z = dot(r1.zw, r1.zw);
  r1.z = cmp(r1.z >= 9.99999975e-005);
  r0.w = r0.w ? r1.z : 0;
  r1.z = dot(r2.yzw, -r4.xyw);
  r1.z = cmp(0 < r1.z);
  r0.w = r0.w ? r1.z : 0;
  if (r0.w != 0) {
    r1.zw = screen_size.zw * r1.xy;
    r2.xy = screen_size.xy / SSRFactor();
    r0.z = r4.z;
    r7.w = r5.z;
    r3.yzw = r7.yzw + -r0.xyz;
    r0.w = saturate(-inv_view._m21);
    r2.z = g_viewport_dimensions.x * (0.5f / SSRFactor());
    r0.w = 1 + -r0.w;
    r2.w = r2.z * r0.w;
    r4.x = -g_viewport_dimensions.x * (0.5f / SSRFactor()) + r1.x;
    r2.w = g_viewport_dimensions.x * (0.5f / SSRFactor()) + -r2.w;
    r0.w = -r2.z * r0.w + abs(r4.x);
    r2.z = 1 / r2.w;
    r0.w = saturate(r2.z * r0.w);
    r2.z = r0.w * -2 + 3;
    r0.w = r0.w * r0.w;
    r0.w = r2.z * r0.w;
    r2.z = cmp(0 < r4.x);
    r2.w = cmp(r4.x < 0);
    r2.z = (int)-r2.z + (int)r2.w;
    r4.xy = cmp(float2(0,0) < r3.yw);
    r4.zw = cmp(r3.yw < float2(0,0));
    r4.xy = (int2)-r4.xy + (int2)r4.zw;
    r2.z = cmp((int)r2.z == (int)r4.x);
    r2.z = r2.z ? 1.000000 : 0;
    r0.w = r2.z * -r0.w + 1;
    r3.x = r3.y * r0.w;
    r2.zw = floor(g_viewport_dimensions.xy / SSRFactor());
    r4.xz = cmp(r3.xz >= float2(0,0));
    r4.xz = r4.xz ? float2(1,1) : 0;
    r5.xy = cmp(r3.xz < float2(0,0));
    r5.xy = r5.xy ? float2(-1,-1) : float2(-0,-0);
    r1.xy = r5.xy + r1.xy;
    r1.xy = r1.xy + r4.xz;
    r1.xy = r1.xy + -r0.xy;
    r1.xy = r1.xy / r3.xz;
    r0.w = min(r1.x, r1.y);
    r0.xyz = r3.xzw * r0.www + r0.xyz;
    r0.w = (int)r4.y;
    r6.xy = r0.xy;
    r7.x = r0.z;
    r1.xy = float2(0,0);
    r3.y = 0;
    while (true) {
      r4.y = cmp(r1.x < 0);
      r3.y = 0;
      if (r4.y != 0) break;
      r4.y = 1 + r1.y;
      r4.w = cmp(100 < r1.y);
      r5.zw = cmp(r6.xy < float2(0,0));
      r5.z = (int)r5.w | (int)r5.z;
      r4.w = (int)r4.w | (int)r5.z;
      r5.zw = cmp(r6.xy >= r2.zw);
      r5.z = (int)r5.w | (int)r5.z;
      r4.w = (int)r4.w | (int)r5.z;
      r5.z = cmp(0 >= r7.x);
      r4.w = (int)r4.w | (int)r5.z;
      r5.z = cmp(r7.x >= 0.999899983);
      r4.w = (int)r4.w | (int)r5.z;
      if (r4.w != 0) {
        r3.y = -1;
        break;
      }
      r8.zw = (int2)r1.xx;
      r5.z = 1 << (int)r8.w;
      r5.z = (int)r5.z;
      r9.xy = r6.xy / r5.zz;
      r9.xy = floor(r9.xy);
      r8.xy = (int2)r9.xy;
      r8.zw = t_hierarchical_depth.Load(r8.xyz).xy;
      r8.zw = r8.zw + -r7.xx;
      r9.zw = r8.zw / r3.ww;
      r5.w = min(r9.z, r9.w);
      r5.w = max(0, r5.w);
      r8.zw = float2(-0.000500000024,0.000500000024) + r8.zw;
      r6.z = r7.x;
      r7.xyz = r3.wxz * r5.www + r6.zxy;
      r9.zw = r7.yz / r5.zz;
      r9.zw = floor(r9.zw);
      r9.zw = (int2)r9.zw;
      r8.zw = r8.zw * r0.ww;
      r8.zw = cmp(r8.zw < float2(0,0));
      r5.w = r8.w ? r8.z : 0;
      r8.xy = cmp((int2)r8.xy != (int2)r9.zw);
      r6.w = (int)r8.y | (int)r8.x;
      r5.w = (int)r5.w | (int)r6.w;
      if (r5.w != 0) {
        r8.xy = r9.xy * r5.zz + r5.xy;
        r8.xy = float2(0.5,0.5) + r8.xy;
        r5.zw = r4.xz * r5.zz + r8.xy;
        r5.zw = r5.zw + -r6.xy;
        r5.zw = r5.zw / r3.xz;
        r5.z = min(r5.z, r5.w);
        r7.xyz = r3.wxz * r5.zzz + r6.zxy;
        r5.z = 2 + r1.x;
        r1.x = min(7, r5.z);
        r6.xy = r7.yz;
      } else {
        r6.xy = r7.yz;
      }
      r1.x = -1 + r1.x;
      r1.y = r4.y;
      r3.y = r4.w;
    }
    r0.xy = cmp(r6.xy < float2(0,0));
    r0.x = (int)r0.y | (int)r0.x;
    r0.yz = cmp(r6.xy >= r2.zw);
    r0.y = (int)r0.z | (int)r0.y;
    r0.x = (int)r0.y | (int)r0.x;
    r0.y = cmp(0 >= r7.x);
    r0.x = (int)r0.y | (int)r0.x;
    r0.y = cmp(r7.x >= 0.999899983);
    r0.x = (int)r0.y | (int)r0.x;
    r0.x = (int)r3.y | (int)r0.x;
    r6.z = 1;
    r0.xzw = r0.xxx ? float3(0,0,0) : r6.zxy;
    r0.zw = r0.zw / r2.xy;
    r0.zw = -r1.zw * SSRFactor() + r0.zw;
    r0.z = dot(r0.zw, r0.zw);
    r0.z = sqrt(r0.z);
    r0.z = exp2(-r0.z);
    r0.y = 65504 * r0.z;
  } else {
    r0.xy = float2(0,9.99999975e-006);
  }
  t_rw_hit[vThreadID.xy] = r0.xy;
  return;
}