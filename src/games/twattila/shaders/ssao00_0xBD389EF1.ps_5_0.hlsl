// ssao.hlsl ps_pixel_resolve (sm_3_0): the game's HBAO (4 directions), the replacement; with AO_BITMASK, RenoDX's
// ambient occlusion with visibility bitmasks (bitmask_ssao.hlsli, its history output at SV_Target1), the addon's own shader.
#include "../shared.h"

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
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}

cbuffer ssao_resolve_PS : register(b1)
{
  float g_NumSteps : packoffset(c0);
  float g_NumDir : packoffset(c0.y);
  float g_R : packoffset(c0.z);
  float g_ExternalAngleBias : packoffset(c0.w);
  float g_Attenuation : packoffset(c1);
  float g_ExternalContrast : packoffset(c1.y);
  float2 g_AOResolution : packoffset(c1.z);
  float g_VerticalFieldOfView : packoffset(c2);
  float g_Strength : packoffset(c2.y);
  float g_MinStrength : packoffset(c2.z);
  float g_JitterResolution : packoffset(c2.w);
}

SamplerState gbuffer_channel_4_sampler_s : register(s0);
SamplerState jitter_texture_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_sampler : register(t0);
Texture2D<float4> jitter_texture_sampler : register(t1);

#define cmp -

// The game's HBAO (3Dmigoto decompile, unchanged).
float4 GameAmbientOcclusion(float4 v0)
{
  float4 o0;
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24,r25;

  r0.x = 0.5 * g_VerticalFieldOfView;
  sincos(r0.x, r0.x, r1.x);
  r0.x = r0.x / r1.x;
  r0.y = 1 / r0.x;
  r0.z = g_AOResolution.y / g_AOResolution.x;
  r0.x = r0.y * r0.z;
  r0.zw = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r0.zw;
  r2.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, 0).yzxw;
  r2.xy = r1.xy * float2(2,-2) + float2(-1,1);
  r2.w = 1;
  r1.z = dot(r2.xyzw, inv_projection._m02_m12_m22_m32);
  r1.w = dot(r2.xyzw, inv_projection._m03_m13_m23_m33);
  r3.z = r1.z / r1.w;
  r1.z = 0.5 * g_R;
  r1.zw = r1.zz * r0.xy;
  r1.zw = r1.zw / r3.zz;
  r2.zw = g_AOResolution.xy * r1.zw;
  r2.z = min(r2.z, r2.w);
  r2.z = min(5, r2.z);
  r2.w = cmp(r2.z >= 1);
  if (r2.w != 0) {
    r2.w = 1 / g_R;
    r3.w = g_R * g_R;
    r4.x = 0.0174532924 * g_ExternalAngleBias;
    sincos(r4.x, r5.x, r6.x);
    r4.y = r5.x / r6.x;
    r4.x = sin(-r4.x);
    r4.x = 1 + r4.x;
    r4.x = g_ExternalContrast / r4.x;
    r0.xy = float2(1,1) / r0.xy;
    r5.xy = float2(1,1) / g_AOResolution.xy;
    r2.xy = r2.xy * r0.xy;
    r3.xy = r2.xy * r3.zz;
    r2.x = 1 + r2.z;
    r1.zw = r1.zw / r2.xx;
    r5.z = 0;
    r6.xyzw = r0.zwzw * g_screen_size.zwzw + r5.xzzy;
    r7.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r6.xy, 0).yzxw;
    r7.xy = r6.xy * float2(2,-2) + float2(-1,1);
    r7.w = 1;
    r2.x = dot(r7.xyzw, inv_projection._m02_m12_m22_m32);
    r2.y = dot(r7.xyzw, inv_projection._m03_m13_m23_m33);
    r8.z = r2.x / r2.y;
    r2.xy = r7.xy * r0.xy;
    r8.xy = r2.xy * r8.zz;
    r5.w = -r5.x;
    r2.xy = r0.zw * g_screen_size.zw + r5.wz;
    r7.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r2.xy, 0).yzxw;
    r7.xy = r2.xy * float2(2,-2) + float2(-1,1);
    r7.w = 1;
    r2.x = dot(r7.xyzw, inv_projection._m02_m12_m22_m32);
    r2.y = dot(r7.xyzw, inv_projection._m03_m13_m23_m33);
    r9.z = r2.x / r2.y;
    r2.xy = r7.xy * r0.xy;
    r9.xy = r2.xy * r9.zz;
    r7.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r6.zw, 0).yzxw;
    r7.xy = r6.zw * float2(2,-2) + float2(-1,1);
    r7.w = 1;
    r2.x = dot(r7.xyzw, inv_projection._m02_m12_m22_m32);
    r2.y = dot(r7.xyzw, inv_projection._m03_m13_m23_m33);
    r6.z = r2.x / r2.y;
    r2.xy = r7.xy * r0.xy;
    r6.xy = r2.xy * r6.zz;
    r2.xy = r5.zy * float2(1,-1) + r1.xy;
    r7.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r2.xy, 0).yzxw;
    r7.xy = r2.xy * float2(2,-2) + float2(-1,1);
    r7.w = 1;
    r2.x = dot(r7.xyzw, inv_projection._m02_m12_m22_m32);
    r2.y = dot(r7.xyzw, inv_projection._m03_m13_m23_m33);
    r10.z = r2.x / r2.y;
    r2.xy = r7.xy * r0.xy;
    r10.xy = r2.xy * r10.zz;
    r7.xyz = r8.xyz + -r3.xyz;
    r8.xyz = -r9.xyz + r3.xyz;
    r2.x = dot(r7.xyz, r7.xyz);
    r2.y = dot(r8.xyz, r8.xyz);
    r2.x = cmp(r2.x < r2.y);
    r7.xyz = r2.xxx ? r7.xyz : r8.xyz;
    r6.xyz = r6.xyz + -r3.xyz;
    r8.xyz = -r10.xyz + r3.xyz;
    r2.x = dot(r6.xyz, r6.xyz);
    r2.y = dot(r8.xyz, r8.xyz);
    r2.x = cmp(r2.x < r2.y);
    r6.xyz = r2.xxx ? r6.xyz : r8.xyz;
    r2.x = g_AOResolution.y * r5.x;
    r6.xyz = r6.xyz * r2.xxx;
    r2.xy = g_JitterResolution * r5.xy;
    r1.xy = r1.xy / r2.xy;
    r8.xyzw = jitter_texture_sampler.SampleLevel(jitter_texture_sampler_s, r1.xy, 0).xyzw;
    r8.xyz = r8.xyz * float3(2,2,2) + float3(-1,-1,-1);
    r1.xy = r8.xy * r1.zw;
    r9.yz = r8.xx * float2(-4.37113883e-008,-8.74227766e-008) + -r8.yy;
    r9.w = r8.y * -4.37113883e-008 + r8.x;
    r9.x = -r8.y * -8.74227766e-008 + -r8.x;
    r10.xyzw = r9.ywxz * r1.zwzw;
    r2.x = r8.x * 1.19248806e-008 + r8.y;
    r2.y = r8.y * 1.19248806e-008 + -r8.x;
    r4.zw = r2.xy * r1.zw;
    r5.zw = r0.zw * g_screen_size.zw + r1.xy;
    r1.xy = r8.zz * r1.xy + r5.zw;
    r11.xyzw = r0.zwzw * g_screen_size.zwzw + r10.xyzw;
    r10.xyzw = r8.zzzz * r10.xyzw + r11.xyzw;
    r5.zw = r0.zw * g_screen_size.zw + r4.zw;
    r4.zw = r8.zz * r4.zw + r5.zw;
    r5.zw = -r0.zw * g_screen_size.zw + r1.xy;
    r5.zw = r5.zw * g_AOResolution.xy + float2(0.5,0.5);
    r5.zw = floor(r5.zw);
    r5.zw = r5.zw * r5.xy;
    r11.xyz = r5.www * r6.xyz;
    r11.xyz = r5.zzz * r7.xyz + r11.xyz;
    r12.xyzw = -r0.zwzw * g_screen_size.zwzw + r10.xyzw;
    r12.xyzw = r12.xyzw * g_AOResolution.xyxy + float4(0.5,0.5,0.5,0.5);
    r12.xyzw = floor(r12.xyzw);
    r12.xyzw = r12.xyzw * r5.xyxy;
    r13.xyz = r12.yyy * r6.xyz;
    r13.xyz = r12.xxx * r7.xyz + r13.xyz;
    r12.xyw = r12.www * r6.xyz;
    r12.xyz = r12.zzz * r7.xyz + r12.xyw;
    r5.zw = -r0.zw * g_screen_size.zw + r4.zw;
    r5.zw = r5.zw * g_AOResolution.xy + float2(0.5,0.5);
    r5.zw = floor(r5.zw);
    r5.zw = r5.zw * r5.xy;
    r14.xyz = r5.www * r6.xyz;
    r14.xyz = r5.zzz * r7.xyz + r14.xyz;
    r5.z = dot(r11.xy, r11.xy);
    r5.z = sqrt(r5.z);
    r5.z = -r11.z / r5.z;
    r5.w = dot(r13.xy, r13.xy);
    r5.w = sqrt(r5.w);
    r5.w = -r13.z / r5.w;
    r5.zw = r5.zw + r4.yy;
    r6.w = dot(r12.xy, r12.xy);
    r6.w = sqrt(r6.w);
    r6.w = -r12.z / r6.w;
    r6.w = r6.w + r4.y;
    r7.w = dot(r14.xy, r14.xy);
    r7.w = sqrt(r7.w);
    r7.w = -r14.z / r7.w;
    r7.w = r7.w + r4.y;
    r11.w = 1;
    r12.w = 1;
    r13.w = 1;
    r14.w = 1;
    r15.xy = r5.zw;
    r15.z = r6.w;
    r15.w = r7.w;
    r16.xyzw = float4(0,0,0,0);
    r8.zw = r1.xy;
    r17.xyzw = r10.xyzw;
    r18.xy = r4.zw;
    r18.zw = float2(0,0);
    while (true) {
      r19.x = cmp(r18.w >= r2.z);
      if (r19.x != 0) break;
      r19.xy = g_AOResolution.xy * r8.zw;
      r19.xy = frac(r19.xy);
      r19.xy = float2(-0.5,-0.5) + r19.xy;
      r19.xy = -r19.xy * r5.xy + r8.zw;
      r19.zw = g_AOResolution.xy * r17.xy;
      r19.zw = frac(r19.zw);
      r19.zw = float2(-0.5,-0.5) + r19.zw;
      r19.zw = -r19.zw * r5.xy + r17.xy;
      r20.xy = g_AOResolution.xy * r17.zw;
      r20.xy = frac(r20.xy);
      r20.xy = float2(-0.5,-0.5) + r20.xy;
      r20.xy = -r20.xy * r5.xy + r17.zw;
      r20.zw = g_AOResolution.xy * r18.xy;
      r20.zw = frac(r20.zw);
      r20.zw = float2(-0.5,-0.5) + r20.zw;
      r20.zw = -r20.zw * r5.xy + r18.xy;
      r21.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r19.xy, 0).xyzw;
      r11.xy = r19.xy * float2(2,-2) + float2(-1,1);
      r11.z = r21.x;
      r21.x = dot(r11.xyzw, inv_projection._m02_m12_m22_m32);
      r11.z = dot(r11.xyzw, inv_projection._m03_m13_m23_m33);
      r21.z = r21.x / r11.z;
      r11.xy = r11.xy * r0.xy;
      r21.xy = r11.xy * r21.zz;
      r22.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r19.zw, 0).xyzw;
      r12.xy = r19.zw * float2(2,-2) + float2(-1,1);
      r12.z = r22.x;
      r11.z = dot(r12.xyzw, inv_projection._m02_m12_m22_m32);
      r12.z = dot(r12.xyzw, inv_projection._m03_m13_m23_m33);
      r22.z = r11.z / r12.z;
      r12.xy = r12.xy * r0.xy;
      r22.xy = r12.xy * r22.zz;
      r23.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r20.xy, 0).xyzw;
      r13.xy = r20.xy * float2(2,-2) + float2(-1,1);
      r13.z = r23.x;
      r11.z = dot(r13.xyzw, inv_projection._m02_m12_m22_m32);
      r12.z = dot(r13.xyzw, inv_projection._m03_m13_m23_m33);
      r23.z = r11.z / r12.z;
      r13.xy = r13.xy * r0.xy;
      r23.xy = r13.xy * r23.zz;
      r24.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r20.zw, 0).xyzw;
      r14.xy = r20.zw * float2(2,-2) + float2(-1,1);
      r14.z = r24.x;
      r11.z = dot(r14.xyzw, inv_projection._m02_m12_m22_m32);
      r12.z = dot(r14.xyzw, inv_projection._m03_m13_m23_m33);
      r24.z = r11.z / r12.z;
      r14.xy = r14.xy * r0.xy;
      r24.xy = r14.xy * r24.zz;
      r8.zw = r8.xy * r1.zw + r8.zw;
      r17.xy = r9.yw * r1.zw + r17.xy;
      r17.zw = r9.xz * r1.zw + r17.zw;
      r18.xy = r2.xy * r1.zw + r18.xy;
      r11.z = -r21.z + r3.z;
      r11.xy = r11.xy * r21.zz + -r3.xy;
      r11.x = dot(r11.xy, r11.xy);
      r11.x = sqrt(r11.x);
      r25.x = r11.z / r11.x;
      r11.x = -r22.z + r3.z;
      r11.yz = r12.xy * r22.zz + -r3.xy;
      r11.y = dot(r11.yz, r11.yz);
      r11.y = sqrt(r11.y);
      r25.y = r11.x / r11.y;
      r11.x = -r23.z + r3.z;
      r11.yz = r13.xy * r23.zz + -r3.xy;
      r11.y = dot(r11.yz, r11.yz);
      r11.y = sqrt(r11.y);
      r25.z = r11.x / r11.y;
      r11.x = -r24.z + r3.z;
      r11.yz = r14.xy * r24.zz + -r3.xy;
      r11.y = dot(r11.yz, r11.yz);
      r11.y = sqrt(r11.y);
      r25.w = r11.x / r11.y;
      r11.xyz = r21.xyz + -r3.xyz;
      r21.x = dot(r11.xyz, r11.xyz);
      r11.xyz = r22.xyz + -r3.xyz;
      r21.y = dot(r11.xyz, r11.xyz);
      r11.xyz = r23.xyz + -r3.xyz;
      r21.z = dot(r11.xyz, r11.xyz);
      r11.xyz = r24.xyz + -r3.xyz;
      r21.w = dot(r11.xyz, r11.xyz);
      r22.xyzw = r25.xyzw * r25.xyzw + float4(1,1,1,1);
      r22.xyzw = sqrt(r22.xyzw);
      r22.xyzw = r25.xyzw / r22.xyzw;
      r23.xyzw = sqrt(r21.xyzw);
      r23.xyzw = r23.xyzw * r2.wwww;
      r23.xyzw = r23.xyzw * r23.xyzw;
      r23.xyzw = -r23.xyzw * g_Attenuation + float4(1,1,1,1);
      r11.xy = -r0.zw * g_screen_size.zw + r19.xy;
      r12.xyz = r11.yyy * r6.xyz;
      r11.xyz = r11.xxx * r7.xyz + r12.xyz;
      r12.xy = -r0.zw * g_screen_size.zw + r19.zw;
      r13.xyz = r12.yyy * r6.xyz;
      r12.xyz = r12.xxx * r7.xyz + r13.xyz;
      r13.xy = -r0.zw * g_screen_size.zw + r20.xy;
      r14.xyz = r13.yyy * r6.xyz;
      r13.xyz = r13.xxx * r7.xyz + r14.xyz;
      r14.xy = -r0.zw * g_screen_size.zw + r20.zw;
      r19.xyz = r14.yyy * r6.xyz;
      r14.xyz = r14.xxx * r7.xyz + r19.xyz;
      r11.x = dot(r11.xy, r11.xy);
      r11.x = sqrt(r11.x);
      r11.x = -r11.z / r11.x;
      r11.y = dot(r12.xy, r12.xy);
      r11.y = sqrt(r11.y);
      r11.y = -r12.z / r11.y;
      r11.z = dot(r13.xy, r13.xy);
      r11.z = sqrt(r11.z);
      r11.z = -r13.z / r11.z;
      r11.xyz = r11.xyz + r4.yyy;
      r12.x = dot(r14.xy, r14.xy);
      r12.x = sqrt(r12.x);
      r12.x = -r14.z / r12.x;
      r12.x = r12.x + r4.y;
      r12.y = r11.x * r11.x + 1;
      r12.y = sqrt(r12.y);
      r19.x = r11.x / r12.y;
      r11.x = r11.y * r11.y + 1;
      r11.x = sqrt(r11.x);
      r19.y = r11.y / r11.x;
      r11.x = r11.z * r11.z + 1;
      r11.x = sqrt(r11.x);
      r19.z = r11.z / r11.x;
      r11.x = r12.x * r12.x + 1;
      r11.x = sqrt(r11.x);
      r19.w = r12.x / r11.x;
      r19.xyzw = r22.xyzw + -r19.xyzw;
      r20.xyzw = cmp(r21.xyzw < r3.wwww);
      r20.xyzw = r20.xyzw ? float4(1,1,1,1) : 0;
      r21.xyzw = cmp(r15.xyzw < r25.xyzw);
      r21.xyzw = r21.xyzw ? float4(1,1,1,1) : 0;
      r20.xyzw = r21.xyzw * r20.xyzw;
      r21.xyzw = r19.xyzw + -r16.xyzw;
      r21.xyzw = r23.xyzw * r21.xyzw;
      r11.x = dot(r20.xyzw, r21.xyzw);
      r18.z = r18.z + r11.x;
      r20.xyzw = cmp(float4(0,0,0,0) < r20.xyzw);
      r16.xyzw = r20.xyzw ? r19.xyzw : r16.xyzw;
      r15.xyzw = r20.xyzw ? r25.xyzw : r15.xyzw;
      r18.w = 1 + r18.w;
    }
    r0.x = r18.z * r4.x;
    r0.x = saturate(-r0.x * 0.25 + 1);
    r0.x = log2(r0.x);
    r0.x = g_Strength * r0.x;
    r0.x = exp2(r0.x);
    r0.y = 1 + -g_MinStrength;
    o0.xyzw = r0.xxxx * r0.yyyy + g_MinStrength;
  } else {
    o0.xyzw = float4(1,1,1,1);
  }
  return o0;
}

#ifdef AO_BITMASK
// The addon's own shader (ao_bitmask.ps_5_0.hlsl, twattila/ao_pass.hpp draws it in this pass's place when the Visibility
// Bitmask is on).
// Attila: Radius in metres, Strength GT-VBAO's own (bitmask_ssao.hlsli).
#include "../bitmask_ssao.hlsli"

// Its later passes (twattila/ao_pass.hpp): AO_RESOLVE Temporal Smoothing (ao_resolve*.ps_5_0.hlsl), AO_DENOISE the
// Denoiser (ao_denoise*.ps_5_0.hlsl), AO_UPSAMPLE into the game's target (ao_upsample*.ps_5_0.hlsl).
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
#if defined(AO_RESOLVE)
  o0 = BitmaskResolve(v0);
#elif defined(AO_DENOISE)
  o0 = BitmaskDenoise(v0);
#elif defined(AO_UPSAMPLE)
  o0 = BitmaskUpsample(v0);
#else
  o0 = BitmaskAmbientOcclusion(v0);
#endif
}
#else
// The replacement: the game's code only (Ambient Occlusion Game's costs what it did); none when the ray-traced ambient
// occlusion replaces it (twattila/rt_scene.hpp multiplies it in after this pass).
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  o0 = (shader_injection.rt_ambient_alone != 0.f) ? 1.f : GameAmbientOcclusion(v0);
}
#endif
