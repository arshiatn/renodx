// environment_specular_light.hlsl cs50_resolve (3Dmigoto decompile, checked against the disassembly): the
// reflections of the screen-space march (cs50_raycast's hits in t5, the lit scene's mips in t6) and the environment
// cubes; out: what the march adds over the cubes (the scene's light already has those), water the whole reflection.
// RenoDX: the ray-traced reflections and specular occlusion (twattila/rt_reflection.hlsli, rt_scene.hpp).

cbuffer environment_PS : register(b0)
{
  float g_environment_weights[8] : packoffset(c0);
  float g_environment_indices[8] : packoffset(c8);
  float g_environment_count : packoffset(c15.y);
}

cbuffer camera : register(b1)
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
}

SamplerState s_environment_array_s : register(s0);
SamplerState gbuffer_channel_1_sampler_s : register(s1);
SamplerState gbuffer_channel_3_sampler_s : register(s2);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
SamplerState s_trilinear_sampler_s : register(s4);
TextureCubeArray<float4> t_environment_array : register(t0);
Texture2D<float4> gbuffer_channel_1_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);
Texture2D<float4> t_jitter : register(t4);
Texture2D<float4> t_hit : register(t5);
Texture2D<float4> t_in_radiance : register(t6);
RWTexture2D<float3> t_out_radiance : register(u0);


// 3Dmigoto declarations
#define cmp -

#include "../../twattila/rt_reflection.hlsli"
#include "../../twattila/render_scale.hlsli"


[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = (int2)vThreadID.xy;
  r0.xy = float2(0.5,0.5) + r0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  uint rt_width, rt_height;
  gbuffer_channel_1_texture.GetDimensions(rt_width, rt_height);
  const int2 rt_texel = int2(r0.zw * float2(rt_width, rt_height));
  r1.xyzw = gbuffer_channel_3_texture.SampleLevel(gbuffer_channel_3_sampler_s, r0.zw, 0).xyzw;
  r1.w = 255 * r1.w;
  r1.w = round(r1.w);
  r1.w = (uint)r1.w;
  r2.x = (int)r1.w & 65;
  if (r2.x != 0) {
    r2.xyz = gbuffer_channel_1_texture.SampleLevel(gbuffer_channel_1_sampler_s, r0.zw, 0).xyz;
    r3.z = gbuffer_channel_4_texture.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, 0).x;
    r2.xy = r2.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
    sincos(r2.xy, r2.xy, r4.xy);
    r5.x = r4.y;
    r5.z = r2.y;
    r4.xz = r5.xz * r4.xx;
    r2.y = 1 + -r2.z;
    // RenoDX: below 100% the view is the top-left share of the targets (r0.zw a texture uv): the position from the view's
    // uv, as view_share patches the game's own bytes (NDC at instruction 17; the projected uv below at 67).
    r3.xy = (r0.zw / FrameRenderShare()) * float2(2,-2) + float2(-1,1);
    r3.w = 1;
    r5.x = dot(r3.xyzw, inv_view_projection._m00_m10_m20_m30);
    r5.y = dot(r3.xyzw, inv_view_projection._m01_m11_m21_m31);
    r5.z = dot(r3.xyzw, inv_view_projection._m02_m12_m22_m32);
    r2.z = dot(r3.xyzw, inv_view_projection._m03_m13_m23_m33);
    r3.xyz = r5.xyz / r2.zzz;
    r3.w = 1;
    r5.x = dot(r3.xyzw, view._m00_m10_m20_m30);
    r5.y = dot(r3.xyzw, view._m01_m11_m21_m31);
    r5.z = dot(r3.xyzw, view._m02_m12_m22_m32);
    r3.xyz = camera_position.xyz + -r3.xyz;
    r2.z = dot(r3.xyz, r3.xyz);
    r2.z = sqrt(r2.z);
    r3.xyz = r3.xyz / r2.zzz;
    r4.y = r2.x;
    r2.x = dot(-r3.xyz, r4.xyz);
    r2.x = r2.x + r2.x;
    r6.xyz = r4.xyz * -r2.xxx + -r3.xyz;
    r2.xz = g_screen_size.xy * r0.zw;
    r2.xz = float2(0.0078125,0.0078125) * r2.xz;
    r7.xy = cmp(r2.xz >= -r2.xz);
    r2.xz = frac(abs(r2.xz));
    r2.xz = r7.xy ? r2.xz : -r2.xz;
    r2.xz = float2(64,64) * r2.xz;
    r7.xy = (int2)r2.xz;
    r7.zw = float2(0,0);
    r2.xzw = t_jitter.Load(r7.xyz).xyz;
    r2.xzw = r2.xzw * float3(2,2,2) + float3(-1,-1,-1);
    r3.w = dot(r2.xzw, r2.xzw);
    r3.w = rsqrt(r3.w);
    r2.xzw = r3.www * r2.xzw;
    r2.xzw = r2.xzw * r2.yyy;
    r2.xzw = r2.xzw * float3(0.100000001,0.100000001,0.100000001) + r6.xyz;
    r3.w = dot(r2.xzw, r2.xzw);
    r3.w = rsqrt(r3.w);
    r2.xzw = r3.www * r2.xzw;
    r7.x = dot(r2.xzw, view._m00_m10_m20);
    r7.y = dot(r2.xzw, view._m01_m11_m21);
    r7.z = dot(r2.xzw, view._m02_m12_m22);
    r2.x = cmp(0 < r7.z);
    r2.zw = camera_near_far.yx + -r5.zz;
    r2.zw = r2.zw / r7.zz;
    r2.x = r2.x ? r2.z : r2.w;
    r5.xyz = r7.xyz * r2.xxx + r5.xyz;
    r5.w = 1;
    r7.x = dot(r5.xyzw, projection._m00_m10_m20_m30);
    r7.y = dot(r5.xyzw, projection._m01_m11_m21_m31);
    r2.x = dot(r5.xyzw, projection._m03_m13_m23_m33);
    r2.xz = r7.xy / r2.xx;
    r2.xz = (r2.xz * float2(0.5,-0.5) + float2(0.5,0.5)) * FrameRenderShare();
    r0.xy = -r0.xy * g_screen_size.zw + r2.xz;
    r2.x = dot(r0.xy, r0.xy);
    r2.x = rsqrt(r2.x);
    r0.xy = r2.xx * r0.xy;
    r2.x = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw, 0).y;
    r2.xzw = float3(1.52662433e-005,0.25,0.100000001) * r2.xyy;
    r2.x = log2(r2.x);
    sincos(r2.z, r5.x, r7.x);
    r2.z = r5.x / r7.x;
    r2.z = r2.z * -r2.x;
    r2.z = g_screen_size.x * r2.z;
    r2.z = max(1, r2.z);
    r2.z = log2(r2.z);
    r2.z = min(7, r2.z);
    r5.xy = t_hit.SampleLevel(s_trilinear_sampler_s, r0.zw, r2.z).xy;
    r2.z = saturate(-0.400000006 + r5.x);
    r2.z = 1.66666663 * r2.z;
    r3.w = 1.52662433e-005 * r5.y;
    r3.w = log2(r3.w);
    sincos(r2.w, r5.x, r7.x);
    r2.w = r5.x / r7.x;
    r2.w = r2.w * -r3.w;
    r2.w = g_screen_size.x * r2.w;
    r2.w = max(1, r2.w);
    r2.w = log2(r2.w);
    r2.w = min(5, r2.w);
    r0.xy = r0.xy * -r2.xx + r0.zw;
    r0.xyz = t_in_radiance.SampleLevel(s_trilinear_sampler_s, r0.xy, r2.w).xyz;
    // RenoDX: the rays' light in place of the march's hit, the share of rays that met a surface its confidence.
    const bool rt_reflected = RtReflectionsBound();
    const float4 rt_traced = rt_reflected ? t_rt_reflection.Load(int3(rt_texel, 0)) : 0.f;
    if (rt_reflected) {
      r0.xyz = rt_traced.rgb;
      r2.z = rt_traced.a;
    }
    r0.w = 1 + -r2.y;
    r0.w = r0.w * r0.w;
    r0.w = log2(r0.w);
    r0.w = 1.79999995 * r0.w;
    r0.w = exp2(r0.w);
    r0.w = 1 + -r0.w;
    r0.w = 8 * r0.w;
    r5.xyz = float3(0,0,0);
    r2.x = 0;
    while (true) {
      r2.w = cmp(r2.x >= g_environment_count);
      if (r2.w != 0) break;
      r2.w = (uint)r2.x;
      r6.w = g_environment_indices[(uint)r2.w];
      r7.xyz = t_environment_array.SampleLevel(s_environment_array_s, r6.xyzw, r0.w).xyz;
      r5.xyz = r7.xyz * g_environment_weights[(uint)r2.w] + r5.xyz;
      r2.x = 1 + r2.x;
    }
    // RenoDX: the cubes' light with the specular occlusion; off water the scene keeps its unoccluded share, so the
    // difference comes off the output.
    const float3 rt_environment = r5.xyz;
    if (!rt_reflected) r5.xyz = RtOccludedEnvironment(rt_texel, r5.xyz);
    r0.w = (int)r1.w & 64;
    r2.xw = r0.ww ? float2(1,0) : float2(0,1);
    r6.xyz = r2.xxx * r5.xyz;
    r0.xyz = -r5.xyz * r2.www + r0.xyz;
    r0.xyz = -r2.xxx * r5.xyz + r0.xyz;
    r0.xyz = r2.zzz * r0.xyz + r6.xyz;
    r0.xyz = r2.www * (r5.xyz - rt_environment) + r0.xyz;
    r0.w = dot(r4.xyz, r3.xyz);
    r0.w = min(1, abs(r0.w));
    r1.w = 1 + -r0.w;
    r1.w = log2(r1.w);
    r1.w = 10 * r1.w;
    r1.w = exp2(r1.w);
    r2.x = r2.y * r2.y;
    r2.y = r2.x * r2.x;
    r2.z = r0.w + r0.w;
    r2.x = -r2.x * r2.x + 1;
    r2.w = r0.w * r0.w;
    r2.x = r2.w * r2.x + r2.y;
    r2.x = sqrt(r2.x);
    r0.w = r2.x + r0.w;
    r0.w = r2.z / r0.w;
    r0.w = r0.w * r0.w;
    r2.xyz = saturate(float3(60,60,60) * r1.xyz);
    r2.xyz = r2.xyz + -r1.xyz;
    r1.xyz = r1.www * r2.xyz + r1.xyz;
    r1.xyz = r1.xyz * r0.www;
    r0.xyz = r1.xyz * r0.xyz;
  } else {
    r0.xyz = float3(0,0,0);
  }
  t_out_radiance[vThreadID.xy] = r0.xyz;
  return;
}