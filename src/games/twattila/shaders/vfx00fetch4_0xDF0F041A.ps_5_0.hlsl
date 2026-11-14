/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

#include "../shared.h"
#include "../rt_local_lights.hlsli"
#include "../particle_layer.h"

// particle_vfx_main.hlsl ps_main_high_quality (shadows hardware_fetch4, sm_5_0 (sm_4_0: 3A12A66D)): the lit particles (smoke, dust,
// clouds, flames) with vfx00's edits (twattila/shaders/vfx00_0xACE33D5D.ps_5_0.hlsl, the game's
// hardware_gather_5_0 build). Made by tools/tw/attila_gaps/atg_vfx_make.py from the 3Dmigoto decompile; checked on
// WARP against the game's bytes (test/tw_vfx_builds). With every control at 1 and t49 / b8 / b9 unbound: the game's.

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

cbuffer shadowmap_PS : register(b1)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  float4 g_vSoftShadowBufferSize : packoffset(c1);
  float4x4 g_amHardSplit[4] : packoffset(c2);
  float4x4 g_amSoftSplit[4] : packoffset(c18);
  float2 g_fFadeRange : packoffset(c34);
  float g_sample_bias : packoffset(c34.z);
  float g_iSplitCount : packoffset(c34.w);
  float3 g_shadow_light_direction : packoffset(c35);
  float g_split_distances[5] : packoffset(c36);
  float4 g_split_uv_texture_bounds[4] : packoffset(c41);
}

cbuffer lighting_VS_PS : register(b2)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  float g_hdr_on : packoffset(c11);
  bool g_ssr_enabled : packoffset(c11.y);
}

cbuffer fog_VS_PS : register(b3)
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

cbuffer cb_vfx : register(b4)
{
  float3 g_camera_aligned_x_axis : packoffset(c0);
  float3 g_camera_aligned_y_axis : packoffset(c1);
  float3 g_camera_aligned_z_axis : packoffset(c2);
  int g_view_id : packoffset(c2.w);
}

cbuffer cb_vfx_emitter_constants_common : register(b5)
{
  uint4 behaviour_mask[512] : packoffset(c0);
}

cbuffer cb_vfx_emitter_constants_ps : register(b6)
{
  float4 ps_specular_params[512] : packoffset(c0);
  float4 ps_specular_params1[512] : packoffset(c512);
  float4 ps_receive_shadows_high_quality_params[512] : packoffset(c1024);
  float4 ps_lighting_legacy_params[512] : packoffset(c1536);
  float4 ps_lighting_params[512] : packoffset(c2048);
  float4 ps_alpha_crush_params[512] : packoffset(c2560);
  float4 ps_soft_edge_params[512] : packoffset(c3072);
}

SamplerState s_sky_s : register(s0);
SamplerState s_environment_s : register(s1);
SamplerState sHardShadowBuffer_s : register(s2);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
SamplerState g_sam_diffuse_s : register(s4);
SamplerState g_sam_normal_s : register(s5);
Texture2D<float4> sHardShadowBuffer : register(t0);
TextureCube<float4> s_environment : register(t1);
Texture2D<float4> gbuffer_channel_4_sampler : register(t2);
TextureCube<float4> s_sky : register(t3);
Texture2DArray<float4> g_tex_diffuse : register(t4);
Texture2DArray<float4> g_tex_normal : register(t5);
// Ray-Traced Shadows Only (rt_scene.hpp OnShadowReaderDraw): the rays' sun occlusion at the pixel (of the surface behind
// the particle); unbound (reads 0) otherwise. Drawn into Particles at Half Resolution's layer (particle_layer.hpp, b8 bound):
// at its render pixel (x layer_scale); otherwise b8 reads 0, x 1.
Texture2D<float> rt_sun_occlusion : register(t49);


// 3Dmigoto declarations
#define cmp -


#if __SHADER_TARGET_MAJOR >= 5
[earlydepthstencil]
#endif
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  nointerpolation uint4 v2 : TEXCOORD0,
  float2 v3 : TEXCOORD1,
  float2 w3 : LOCAL_POS0,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float3 v7 : TEXCOORD5,
  float3 v8 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21,r22,r23,r24,r25,r26,r27,r28,r29;
  uint4 bitmask, uiDest;
  float4 fDest;
  float3 vfxUnlitColour;

  r0.x = v2.x;
  r0.y = (uint)ps_alpha_crush_params[r0.x].x;
  r1.xyzw = int4(0x20000,0x400000,0x80000,0x800000) & (int4)behaviour_mask[r0.x].xxxx;
  if (r1.x != 0) {
    r2.xyz = ddx(v3.xyx);
    r3.xyz = ddy(v3.xyx);
    r4.z = (uint)behaviour_mask[r0.x].y;
    r4.xy = v3.xy;
    r5.xyzw = g_tex_diffuse.SampleGrad(g_sam_diffuse_s, r4.xyz, r2.xy, r3.xy).xyzw;
    r4.xy = v8.xy;
    r2.xyzw = g_tex_diffuse.SampleGrad(g_sam_diffuse_s, r4.xyz, r2.xy, r3.xy).xyzw;
    r2.xyzw = r2.xyzw + -r5.xyzw;
    r2.xyzw = v8.zzzz * r2.xyzw + r5.xyzw;
    r3.xyz = min(r2.xyz, r2.www);
    r3.xyz = v1.xyz * r3.xyz;
    r3.xyz = v1.www * r3.xyz;
    r3.w = v1.w * r2.w;
    r2.xyz = saturate(r2.xyz + -r2.www);
    r4.xyz = v1.xyz * v1.www;
    r2.xyz = r4.xyz * r2.xyz;
  } else {
    r3.xyzw = v1.xyzw;
    r2.xyz = float3(0,0,0);
  }
  r0.z = cmp(ps_alpha_crush_params[r0.x].y == ps_alpha_crush_params[r0.x].z);
  r0.w = -ps_alpha_crush_params[r0.x].w + 1;
  r1.x = max(ps_alpha_crush_params[r0.x].y, r0.w);
  r4.xy = float2(3276,4099) * v7.xx;
  r4.xy = frac(r4.xy);
  r1.x = -1 + r1.x;
  r1.x = r4.x * r1.x + 1;
  r0.w = ps_alpha_crush_params[r0.x].z * r0.w;
  r0.w = max(ps_alpha_crush_params[r0.x].y, r0.w);
  r0.w = -ps_alpha_crush_params[r0.x].z + r0.w;
  r0.w = r4.y * r0.w + ps_alpha_crush_params[r0.x].z;
  r0.z = r0.z ? r1.x : r0.w;
  r0.z = -ps_alpha_crush_params[r0.x].y + r0.z;
  r0.z = max(0.00100000005, r0.z);
  r0.w = -ps_alpha_crush_params[r0.x].y + v4.w;
  r0.z = saturate(r0.w / r0.z);
  r0.w = saturate(r3.w + -r0.z);
  r1.x = 1.00100005 + -r0.z;
  r0.w = r0.w / r1.x;
  r0.z = r0.w + -r0.z;
  r0.z = cmp(r0.z >= 0);
  r0.z = r0.z ? 1.000000 : 0;
  r0.z = r0.z * r0.w;
  r4.w = r0.y ? r0.z : r0.w;
  r0.y = cmp(r3.w != 0.000000);
  r0.z = r4.w / r3.w;
  r0.y = r0.y ? r0.z : 1;
  r4.xyz = r3.xyz * r0.yyy;
  r3.xyzw = r1.yyyy ? r4.xyzw : r3.xyzw;
  r4.xyz = r3.xyz + r2.xyz;
  r4.w = r3.w;
  r5.xyzw = cmp(r4.xyzw < float4(0.0199999996,0.0199999996,0.0199999996,0.0199999996));
  r0.yz = r5.zw ? r5.xy : 0;
  r0.y = (int)r0.z & (int)r0.y;
  if (r0.y != 0) discard;

  // RenoDX: VFX Brightness after the stock discard (as vfx00): the additive colour (r2) and the base (r3) in one group,
  // snow (diffuse slice 1) and weather haze (slice 7) their own.
  float materialBrightness = VfxBaseBrightness;
  if ((behaviour_mask[v2.x].x & 0x00020000u) != 0u) {
    uint diffuseSlice = behaviour_mask[v2.x].y;
    if (diffuseSlice == 1u) {
      materialBrightness = VfxSnowBrightness;
    } else if (diffuseSlice == 7u) {
      materialBrightness = VfxWeatherBrightness;
    }
  }
  r2.xyz *= materialBrightness;
  r3.xyz *= materialBrightness;
  r4.xyz = r3.xyz + r2.xyz;
  vfxUnlitColour = r4.xyz;

  r0.y = cmp(0.100000001 < r4.w);
  r5.z = (uint)behaviour_mask[r0.x].z;
  r5.xy = v3.xy;
  r5.xyz = g_tex_normal.Sample(g_sam_normal_s, r5.xyz).xyz;
  r6.xyz = camera_position.xyz + -v4.xyz;
  r0.z = dot(r6.xyz, r6.xyz);
  r0.z = rsqrt(r0.z);
  r6.xyz = r6.xyz * r0.zzz;
  r0.z = min(v5.x, v5.y);
  r0.z = ps_receive_shadows_high_quality_params[r0.x].y * r0.z;
  r7.xyzw = float4(0.166666672,0.333333343,0.5,0.666666687) * r0.zzzz;
  r8.xyz = v4.xyz;
  r8.w = 1;
  r0.w = dot(r8.xyzw, view_projection._m03_m13_m23_m33);
  r9.xyz = -g_shadow_light_direction.xyz * g_sample_bias + v4.xyz;
  r10.x = g_split_distances[1];
  r10.y = g_split_distances[2];
  r10.z = g_split_distances[3];
  r10.w = g_split_distances[4];
  r11.xyzw = cmp(r0.wwww >= r10.xyzw);
  r11.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
  r1.x = dot(r11.xyzw, r11.xyzw);
  r1.y = (int)r1.x;
  r2.w = (uint)r1.y << 2;
  r9.w = 1;
  r11.x = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m00_m10_m20_m30);
  r11.y = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m01_m11_m21_m31);
  r11.z = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m02_m12_m22_m32);
  r2.w = dot(r9.xyzw, g_amHardSplit[r2.w/4]._m03_m13_m23_m33);
  r9.xyz = r11.xyz / r2.www;
  r11.xy = r9.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r11.zw = floor(r11.xy);
  r12.xy = g_vHardShadowBufferSize.zw * r11.zw;
  r12.xy = r12.xy / r2.ww;
  r12.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r12.xy).xyzw;
  r13.xyz = -r7.xxx * r6.xyz + v4.xyz;
  r13.xyz = view._m00_m10_m20 * float3(0.0143827796,0.0143827796,0.0143827796) + r13.xyz;
  r13.xyz = view._m01_m11_m21 * float3(0.0263274908,0.0263274908,0.0263274908) + r13.xyz;
  r13.w = 1;
  r2.w = dot(r13.xyzw, view_projection._m03_m13_m23_m33);
  r13.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r13.xyz;
  r14.xyzw = cmp(r2.wwww >= r10.xyzw);
  r14.xyzw = r14.xyzw ? float4(1,1,1,1) : 0;
  r5.w = dot(r14.xyzw, r14.xyzw);
  r6.w = (int)r5.w;
  r7.x = (uint)r6.w << 2;
  r13.w = 1;
  r14.x = dot(r13.xyzw, g_amHardSplit[r7.x/4]._m00_m10_m20_m30);
  r14.y = dot(r13.xyzw, g_amHardSplit[r7.x/4]._m01_m11_m21_m31);
  r14.z = dot(r13.xyzw, g_amHardSplit[r7.x/4]._m02_m12_m22_m32);
  r7.x = dot(r13.xyzw, g_amHardSplit[r7.x/4]._m03_m13_m23_m33);
  r13.xyz = r14.xyz / r7.xxx;
  r14.xy = r13.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r14.zw = floor(r14.xy);
  r15.xy = g_vHardShadowBufferSize.zw * r14.zw;
  r15.xy = r15.xy / r7.xx;
  r15.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r15.xy).xyzw;
  r16.xyz = -r7.yyy * r6.xyz + v4.xyz;
  r16.xyz = view._m00_m10_m20 * float3(0.0504882,0.0504882,0.0504882) + r16.xyz;
  r16.xyz = view._m01_m11_m21 * float3(0.0324180014,0.0324180014,0.0324180014) + r16.xyz;
  r16.w = 1;
  r7.x = dot(r16.xyzw, view_projection._m03_m13_m23_m33);
  r16.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r16.xyz;
  r17.xyzw = cmp(r7.xxxx >= r10.xyzw);
  r17.xyzw = r17.xyzw ? float4(1,1,1,1) : 0;
  r7.y = dot(r17.xyzw, r17.xyzw);
  r9.w = (int)r7.y;
  r13.w = (uint)r9.w << 2;
  r16.w = 1;
  r17.x = dot(r16.xyzw, g_amHardSplit[r13.w/4]._m00_m10_m20_m30);
  r17.y = dot(r16.xyzw, g_amHardSplit[r13.w/4]._m01_m11_m21_m31);
  r17.z = dot(r16.xyzw, g_amHardSplit[r13.w/4]._m02_m12_m22_m32);
  r13.w = dot(r16.xyzw, g_amHardSplit[r13.w/4]._m03_m13_m23_m33);
  r16.xyz = r17.xyz / r13.www;
  r17.xy = r16.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r17.zw = floor(r17.xy);
  r18.xy = g_vHardShadowBufferSize.zw * r17.zw;
  r18.xy = r18.xy / r13.ww;
  r18.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r18.xy).xyzw;
  r19.xyz = -r7.zzz * r6.xyz + v4.xyz;
  r19.xyz = view._m00_m10_m20 * float3(0.089774698,0.089774698,0.089774698) + r19.xyz;
  r19.xyz = view._m01_m11_m21 * float3(0.00636635954,0.00636635954,0.00636635954) + r19.xyz;
  r19.w = 1;
  r7.z = dot(r19.xyzw, view_projection._m03_m13_m23_m33);
  r19.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r19.xyz;
  r20.xyzw = cmp(r7.zzzz >= r10.xyzw);
  r20.xyzw = r20.xyzw ? float4(1,1,1,1) : 0;
  r13.w = dot(r20.xyzw, r20.xyzw);
  r16.w = (int)r13.w;
  r20.x = (uint)r16.w << 2;
  r19.w = 1;
  r21.x = dot(r19.xyzw, g_amHardSplit[r20.x/4]._m00_m10_m20_m30);
  r21.y = dot(r19.xyzw, g_amHardSplit[r20.x/4]._m01_m11_m21_m31);
  r21.z = dot(r19.xyzw, g_amHardSplit[r20.x/4]._m02_m12_m22_m32);
  r19.x = dot(r19.xyzw, g_amHardSplit[r20.x/4]._m03_m13_m23_m33);
  r19.yzw = r21.xyz / r19.xxx;
  r20.xy = r19.yz * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r20.zw = floor(r20.xy);
  r21.xy = g_vHardShadowBufferSize.zw * r20.zw;
  r21.xy = r21.xy / r19.xx;
  r21.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r21.xy).xyzw;
  r22.xyz = -r7.www * r6.xyz + v4.xyz;
  r22.xyz = view._m00_m10_m20 * float3(0.109115697,0.109115697,0.109115697) + r22.xyz;
  r22.xyz = view._m01_m11_m21 * float3(-0.049937699,-0.049937699,-0.049937699) + r22.xyz;
  r22.w = 1;
  r7.w = dot(r22.xyzw, view_projection._m03_m13_m23_m33);
  r22.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r22.xyz;
  r23.xyzw = cmp(r7.wwww >= r10.xyzw);
  r23.xyzw = r23.xyzw ? float4(1,1,1,1) : 0;
  r19.x = dot(r23.xyzw, r23.xyzw);
  r23.x = (int)r19.x;
  r0.z = 0.833333373 * r0.z;
  r23.y = (uint)r23.x << 2;
  r22.w = 1;
  r24.x = dot(r22.xyzw, g_amHardSplit[r23.y/4]._m00_m10_m20_m30);
  r24.y = dot(r22.xyzw, g_amHardSplit[r23.y/4]._m01_m11_m21_m31);
  r24.z = dot(r22.xyzw, g_amHardSplit[r23.y/4]._m02_m12_m22_m32);
  r22.x = dot(r22.xyzw, g_amHardSplit[r23.y/4]._m03_m13_m23_m33);
  r22.yzw = r24.xyz / r22.xxx;
  r23.yz = r22.yz * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r24.xy = floor(r23.yz);
  r24.zw = g_vHardShadowBufferSize.zw * r24.xy;
  r24.zw = r24.zw / r22.xx;
  r25.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r24.zw).xyzw;
  r6.xyz = -r0.zzz * r6.xyz + v4.xyz;
  r6.xyz = view._m00_m10_m20 * float3(0.0897708014,0.0897708014,0.0897708014) + r6.xyz;
  r26.xyz = view._m01_m11_m21 * float3(-0.120171599,-0.120171599,-0.120171599) + r6.xyz;
  r26.w = 1;
  r0.z = dot(r26.xyzw, view_projection._m03_m13_m23_m33);
  r26.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r26.xyz;
  r10.xyzw = cmp(r0.zzzz >= r10.xyzw);
  r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
  r6.x = dot(r10.xyzw, r10.xyzw);
  r6.y = (int)r6.x;
  r6.z = (uint)r6.y << 2;
  r26.w = 1;
  r10.x = dot(r26.xyzw, g_amHardSplit[r6.z/4]._m00_m10_m20_m30);
  r10.y = dot(r26.xyzw, g_amHardSplit[r6.z/4]._m01_m11_m21_m31);
  r10.z = dot(r26.xyzw, g_amHardSplit[r6.z/4]._m02_m12_m22_m32);
  r6.z = dot(r26.xyzw, g_amHardSplit[r6.z/4]._m03_m13_m23_m33);
  r10.xyz = r10.xyz / r6.zzz;
  r24.zw = r10.xy * g_vHardShadowBufferSize.xy + float2(0.5,0.5);
  r26.xy = floor(r24.zw);
  r26.zw = g_vHardShadowBufferSize.zw * r26.xy;
  r26.zw = r26.zw / r6.zz;
  r27.xyzw = sHardShadowBuffer.Sample(sHardShadowBuffer_s, r26.zw).xyzw;
  if (r0.y != 0) {
    r28.xyz = int3(0x40000,0x100000,0x200000) & (int3)behaviour_mask[r0.x].xxx;
    if (r28.x != 0) {
      r5.xyz = r5.xyz * float3(2,2,2) + float3(-1,-1,-1);
    } else {
      r5.xyz = float3(0,0,1);
    }
    if (VfxNormalStrength != 1.0f) {
      r5.xyz = lerp(float3(0,0,1), r5.xyz, VfxNormalStrength);
    }
    if (r28.y != 0) {
      r0.y = cmp(r1.x < g_iSplitCount);
      if (r0.y != 0) {
        r26.zw = cmp(r9.xy < g_split_uv_texture_bounds[r1.y].xy);
        r0.y = (int)r26.w | (int)r26.z;
        r9.xy = cmp(g_split_uv_texture_bounds[r1.y].zw < r9.xy);
        r6.z = (int)r9.y | (int)r9.x;
        r0.y = (int)r0.y | (int)r6.z;
        if (r0.y == 0) {
          r12.xyzw = cmp(r9.zzzz < r12.xyzw);
          r29.xyzw = r12.xyzw ? float4(1,1,1,1) : 0;
          r9.xy = r11.xy + -r11.zw;
          r0.y = 1 + -r9.x;
          r9.xz = r12.xz ? float2(-1,-1) : float2(-0,-0);
          r9.xz = r29.wy + r9.xz;
          r9.xz = r0.yy * r9.xz + r29.xz;
          r0.y = r9.z + -r9.x;
          r0.y = r9.y * r0.y + r9.x;
        } else {
          r0.y = 1;
        }
      } else {
        r0.y = 1;
      }
      r6.z = -1 + g_iSplitCount;
      r1.x = cmp(r1.x == r6.z);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r1.y+1] + -g_split_distances[r1.y];
        r9.x = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r1.y+1];
        r0.w = -r1.x + r0.w;
        r0.w = saturate(r0.w / r9.x);
        r1.x = 1 + -r0.y;
        r0.y = r0.w * r1.x + r0.y;
      }
      r0.w = cmp(r5.w < g_iSplitCount);
      if (r0.w != 0) {
        r1.xy = cmp(r13.xy < g_split_uv_texture_bounds[r6.w].xy);
        r0.w = (int)r1.y | (int)r1.x;
        r1.xy = cmp(g_split_uv_texture_bounds[r6.w].zw < r13.xy);
        r1.x = (int)r1.y | (int)r1.x;
        r0.w = (int)r0.w | (int)r1.x;
        if (r0.w == 0) {
          r11.xyzw = cmp(r13.zzzz < r15.xyzw);
          r12.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
          r1.xy = r14.xy + -r14.zw;
          r0.w = 1 + -r1.x;
          r9.xy = r11.xz ? float2(-1,-1) : float2(-0,-0);
          r9.xy = r12.wy + r9.xy;
          r9.xy = r0.ww * r9.xy + r12.xz;
          r0.w = r9.y + -r9.x;
          r0.w = r1.y * r0.w + r9.x;
        } else {
          r0.w = 1;
        }
      } else {
        r0.w = 1;
      }
      r1.x = cmp(r5.w == r6.z);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r6.w+1] + -g_split_distances[r6.w];
        r1.y = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r6.w+1];
        r1.x = r2.w + -r1.x;
        r1.x = saturate(r1.x / r1.y);
        r1.y = 1 + -r0.w;
        r0.w = r1.x * r1.y + r0.w;
      }
      r0.yw = saturate(r0.yw);
      r0.y = r0.y + r0.w;
      r0.w = cmp(r7.y < g_iSplitCount);
      if (r0.w != 0) {
        r1.xy = cmp(r16.xy < g_split_uv_texture_bounds[r9.w].xy);
        r0.w = (int)r1.y | (int)r1.x;
        r1.xy = cmp(g_split_uv_texture_bounds[r9.w].zw < r16.xy);
        r1.x = (int)r1.y | (int)r1.x;
        r0.w = (int)r0.w | (int)r1.x;
        if (r0.w == 0) {
          r11.xyzw = cmp(r16.zzzz < r18.xyzw);
          r12.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
          r1.xy = r17.xy + -r17.zw;
          r0.w = 1 + -r1.x;
          r9.xy = r11.xz ? float2(-1,-1) : float2(-0,-0);
          r9.xy = r12.wy + r9.xy;
          r9.xy = r0.ww * r9.xy + r12.xz;
          r0.w = r9.y + -r9.x;
          r0.w = r1.y * r0.w + r9.x;
        } else {
          r0.w = 1;
        }
      } else {
        r0.w = 1;
      }
      r1.x = cmp(r6.z == r7.y);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r9.w+1] + -g_split_distances[r9.w];
        r1.y = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r9.w+1];
        r1.x = r7.x + -r1.x;
        r1.x = saturate(r1.x / r1.y);
        r1.y = 1 + -r0.w;
        r0.w = r1.x * r1.y + r0.w;
      }
      r0.w = saturate(r0.w);
      r0.y = r0.y + r0.w;
      r0.w = cmp(r13.w < g_iSplitCount);
      if (r0.w != 0) {
        r1.xy = cmp(r19.yz < g_split_uv_texture_bounds[r16.w].xy);
        r0.w = (int)r1.y | (int)r1.x;
        r1.xy = cmp(g_split_uv_texture_bounds[r16.w].zw < r19.yz);
        r1.x = (int)r1.y | (int)r1.x;
        r0.w = (int)r0.w | (int)r1.x;
        if (r0.w == 0) {
          r9.xyzw = cmp(r19.wwww < r21.xyzw);
          r11.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
          r1.xy = r20.xy + -r20.zw;
          r0.w = 1 + -r1.x;
          r7.xy = r9.xz ? float2(-1,-1) : float2(-0,-0);
          r7.xy = r11.wy + r7.xy;
          r7.xy = r0.ww * r7.xy + r11.xz;
          r0.w = r7.y + -r7.x;
          r0.w = r1.y * r0.w + r7.x;
        } else {
          r0.w = 1;
        }
      } else {
        r0.w = 1;
      }
      r1.x = cmp(r6.z == r13.w);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r16.w+1] + -g_split_distances[r16.w];
        r1.y = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r16.w+1];
        r1.x = r7.z + -r1.x;
        r1.x = saturate(r1.x / r1.y);
        r1.y = 1 + -r0.w;
        r0.w = r1.x * r1.y + r0.w;
      }
      r0.w = saturate(r0.w);
      r0.y = r0.y + r0.w;
      r0.w = cmp(r19.x < g_iSplitCount);
      if (r0.w != 0) {
        r1.xy = cmp(r22.yz < g_split_uv_texture_bounds[r23.x].xy);
        r0.w = (int)r1.y | (int)r1.x;
        r1.xy = cmp(g_split_uv_texture_bounds[r23.x].zw < r22.yz);
        r1.x = (int)r1.y | (int)r1.x;
        r0.w = (int)r0.w | (int)r1.x;
        if (r0.w == 0) {
          r9.xyzw = cmp(r22.wwww < r25.xyzw);
          r11.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
          r1.xy = -r24.xy + r23.yz;
          r0.w = 1 + -r1.x;
          r7.xy = r9.xz ? float2(-1,-1) : float2(-0,-0);
          r7.xy = r11.wy + r7.xy;
          r7.xy = r0.ww * r7.xy + r11.xz;
          r0.w = r7.y + -r7.x;
          r0.w = r1.y * r0.w + r7.x;
        } else {
          r0.w = 1;
        }
      } else {
        r0.w = 1;
      }
      r1.x = cmp(r6.z == r19.x);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r23.x+1] + -g_split_distances[r23.x];
        r1.y = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r23.x+1];
        r1.x = r7.w + -r1.x;
        r1.x = saturate(r1.x / r1.y);
        r1.y = 1 + -r0.w;
        r0.w = r1.x * r1.y + r0.w;
      }
      r0.w = saturate(r0.w);
      r0.y = r0.y + r0.w;
      r0.w = cmp(r6.x < g_iSplitCount);
      if (r0.w != 0) {
        r1.xy = cmp(r10.xy < g_split_uv_texture_bounds[r6.y].xy);
        r0.w = (int)r1.y | (int)r1.x;
        r1.xy = cmp(g_split_uv_texture_bounds[r6.y].zw < r10.xy);
        r1.x = (int)r1.y | (int)r1.x;
        r0.w = (int)r0.w | (int)r1.x;
        if (r0.w == 0) {
          r7.xyzw = cmp(r10.zzzz < r27.xyzw);
          r9.xyzw = r7.xyzw ? float4(1,1,1,1) : 0;
          r1.xy = -r26.xy + r24.zw;
          r0.w = 1 + -r1.x;
          r7.xy = r7.xz ? float2(-1,-1) : float2(-0,-0);
          r7.xy = r9.wy + r7.xy;
          r7.xy = r0.ww * r7.xy + r9.xz;
          r0.w = r7.y + -r7.x;
          r0.w = r1.y * r0.w + r7.x;
        } else {
          r0.w = 1;
        }
      } else {
        r0.w = 1;
      }
      r1.x = cmp(r6.z == r6.x);
      r1.x = cmp((int)r1.x == -1);
      if (r1.x != 0) {
        r1.x = g_split_distances[r6.y+1] + -g_split_distances[r6.y];
        r1.y = g_fFadeRange.y * r1.x;
        r1.x = -r1.x * g_fFadeRange.y + g_split_distances[r6.y+1];
        r0.z = -r1.x + r0.z;
        r0.z = saturate(r0.z / r1.y);
        r1.x = 1 + -r0.w;
        r0.w = r0.z * r1.x + r0.w;
      }
      r0.w = saturate(r0.w);
      r0.y = r0.y + r0.w;
      r0.y = ps_receive_shadows_high_quality_params[r0.x].x * r0.y;
      r0.y = 0.166666672 * r0.y;
      // RenoDX: the rays' sun occlusion (Ray-Traced Shadows Only; unbound 0: the game's), at the render pixel in the layer.
      r0.y *= 1.f - rt_sun_occlusion.Load(int3(v0.xy * max(PL.layer_scale, 1.f), 0));
    } else {
      r0.y = 1;
    }
    if (VfxShadowStrength != 1.0f) {
      r0.y = lerp(1.0f, r0.y, VfxShadowStrength);
    }
    if (r28.z != 0) {
      r0.z = (uint)ps_lighting_legacy_params[r0.x].x;
      r0.w = cmp(r4.w < 0.200000003);
      r1.x = cmp(0 < g_hdr_on);
      r6.xyz = r1.xxx ? float3(360,180,0.00400000019) : float3(1,0.5,1);
      r7.xyz = sun_colour.xyz * float3(0.25,0.25,0.25) + r6.yyy;
      r9.xyz = r7.xyz * r6.zzz;
      r1.x = saturate(ps_lighting_legacy_params[r0.x].z);
      r7.xyz = -r7.xyz * r6.zzz + ps_lighting_legacy_params[r0.x].zzz;
      r7.xyz = r1.xxx * r7.xyz + r9.xyz;
      r9.xyz = ps_lighting_params[r0.x].yyy * r2.xyz;
      r7.xyz = r7.xyz * r3.xyz + r9.xyz;
      r4.xyz = r0.www ? r7.xyz : r3.xyz;
      r6.yw = v6.zw * -r5.yy;
      r6.yw = r5.xx * v6.xy + r6.yw;
      r7.xyz = g_camera_aligned_y_axis.xyz * r6.www;
      r7.xyz = -r6.yyy * g_camera_aligned_x_axis.xyz + -r7.xyz;
      r7.xyz = -r5.zzz * g_camera_aligned_z_axis.xyz + r7.xyz;
      if (r0.z != 0) {
        r0.z = (uint)ps_lighting_legacy_params[r0.x].y;
        r0.w = dot(-sun_direction.xyz, -sun_direction.xyz);
        r0.w = rsqrt(r0.w);
        r10.xyz = -sun_direction.xyz * r0.www;
        r0.w = dot(r10.xyz, r7.xyz);
        r0.w = max(0, r0.w);
        r0.w = -1 + r0.w;
        r0.w = ps_lighting_params[r0.x].y * r0.w;
        r0.w = v5.w * r0.w + 1;
        r10.xyz = ps_lighting_params[r0.x].yyy * r3.xyz;
        r11.xyz = r10.xyz * r0.www;
        r11.xyz = sun_colour.xyz * r11.xyz;
        r11.xyz = r11.xyz * r6.zzz;
        r12.xyz = ambient_cube_tb[0].xyz * r6.xxx;
        // RenoDX: torches and fires light the smoke near them (ray tracing's local lights; this path lights with the sun's
        // colour as it is, no phase function: theirs too).
        r12.xyz += RtLocalLightAt(v4.xyz);
        r10.xyz = r12.xyz * r10.xyz;
        r10.xyz = r10.xyz * r6.zzz;
        r10.xyz = r0.yyy * r11.xyz + r10.xyz;
        r3.xyz = v5.zzz * r10.xyz;
        r0.z = (uint)r0.z;
        r0.w = r4.w * v5.w + -r3.w;
        r3.w = r0.z * r0.w + r4.w;
      } else {
        r10.xyz = float3(1,4,1) * r7.xyz;
        r0.z = dot(r10.xyz, r10.xyz);
        r0.z = rsqrt(r0.z);
        r10.xyz = r10.xyz * r0.zzz;
        r11.xyz = cmp(r10.xyz < float3(0,0,0));
        r12.xyz = r11.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
        r11.xyw = r11.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
        r13.xyz = r11.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
        r10.xyz = r10.xyz * r10.xyz;
        r11.xyz = r10.yyy * r11.xyw;
        r10.xyw = r10.xxx * r12.xyz + r11.xyz;
        r10.xyz = r10.zzz * r13.xyz + r10.xyw;
        r6.xyw = r10.xyz * r6.xxx;
        r0.z = dot(-sun_direction.xyz, -sun_direction.xyz);
        r0.z = rsqrt(r0.z);
        r10.xyz = -sun_direction.xyz * r0.zzz;
        r0.z = dot(view._m02_m12_m22, view._m02_m12_m22);
        r0.z = rsqrt(r0.z);
        r11.xyz = view._m02_m12_m22 * r0.zzz;
        r0.z = dot(-r10.xyz, r11.xyz);
        r0.w = ps_lighting_params[r0.x].z + ps_lighting_params[r0.x].w;
        r1.y = dot(r10.xyz, r7.xyz);
        r1.y = r1.y * -0.5 + 0.5;
        r1.y = r1.y * -r0.w;
        r1.y = ps_lighting_params[r0.x].x * r1.y;
        r1.y = v5.x * r1.y;
        r1.y = 1.44269502 * r1.y;
        r1.y = exp2(r1.y);
        r2.w = -ps_lighting_legacy_params[r0.x].w + 1;
        r2.w = r2.w * r2.w;
        r2.w = 0.0795774683 * r2.w;
        r5.w = ps_lighting_legacy_params[r0.x].w * ps_lighting_legacy_params[r0.x].w + 1;
        r7.x = dot(r0.zz, ps_lighting_legacy_params[r0.x].ww);
        r5.w = -r7.x + r5.w;
        r5.w = log2(r5.w);
        r5.w = 1.5 * r5.w;
        r5.w = exp2(r5.w);
        r2.w = r2.w / r5.w;
        r2.w = ps_lighting_params[r0.x].w * r2.w;
        r0.z = r0.z * r0.z + 1;
        r0.z = 0.0596831031 * r0.z;
        r0.z = r0.z * ps_lighting_params[r0.x].z + r2.w;
        r0.z = r0.z / r0.w;
        r0.w = 1 + -r1.y;
        r0.z = r0.z * r0.w + r1.y;
        r7.xyz = sun_colour.xyz * r0.zzz;
        r0.yzw = r7.xyz * r0.yyy + r6.xyw;
        // RenoDX: torches and fires light the smoke near them (isotropic: 1 / 4 pi, as the sun's phase
        // functions here).
        r0.yzw += 0.0795774683 * RtLocalLightAt(v4.xyz);
        r6.xyw = r0.yzw * r6.zzz;
        r0.yzw = -r0.yzw * r6.zzz + ps_lighting_legacy_params[r0.x].zzz;
        r0.yzw = r1.xxx * r0.yzw + r6.xyw;
        r3.xyz = r0.yzw * r3.xyz + r9.xyz;
        r3.w = r4.w;
      }
    } else {
      r4.xyz = r3.xyz;
    }
    r0.y = -0.100000001 + r4.w;
    r0.y = 10 * r0.y;
    r0.y = min(1, r0.y);
    r6.xyzw = -r4.xyzw + r3.xyzw;
    r4.xyzw = r0.yyyy * r6.xyzw + r4.xyzw;
  } else {
    r0.y = 0x00200000 & (int)behaviour_mask[r0.x].x;
    r0.z = cmp(0 < g_hdr_on);
    r0.zw = r0.zz ? float2(180,0.00400000019) : float2(0.5,1);
    r6.xyz = sun_colour.xyz * float3(0.25,0.25,0.25) + r0.zzz;
    r7.xyz = r6.xyz * r0.www;
    r0.z = saturate(ps_lighting_legacy_params[r0.x].z);
    r6.xyz = -r6.xyz * r0.www + ps_lighting_legacy_params[r0.x].zzz;
    r6.xyz = r0.zzz * r6.xyz + r7.xyz;
    r2.xyz = ps_lighting_params[r0.x].yyy * r2.xyz;
    r2.xyz = r6.xyz * r3.xyz + r2.xyz;
    r4.xyz = r0.yyy ? r2.xyz : r3.xyz;
    r5.xyz = float3(0,0,1);
  }
  if (VfxLightingStrength != 1.0f
      && (behaviour_mask[v2.x].x & 0x00200000u) != 0u) {
    r4.xyz = lerp(vfxUnlitColour, r4.xyz, VfxLightingStrength);
  }
  if (r1.z != 0) {
    r0.yz = -ps_specular_params1[r0.x].xy + float2(1,1);
    r1.xyz = -camera_position.xyz + v4.xyz;
    r0.w = dot(r1.xyz, r1.xyz);
    r0.w = rsqrt(r0.w);
    r1.xyz = r1.xyz * r0.www;
    r0.w = dot(-r1.xyz, r5.xyz);
    r0.w = r0.w + r0.w;
    r2.xyz = r5.xyz * -r0.www + -r1.xyz;
    r0.w = dot(-sun_direction.xyz, r5.xyz);
    r2.w = cmp(0 < r0.w);
    if (r2.w != 0) {
      r2.w = dot(-sun_direction.xyz, r2.xyz);
      r2.w = max(-1, r2.w);
      r2.w = min(1, r2.w);
      r3.x = 1 + -abs(r2.w);
      r3.x = sqrt(r3.x);
      r3.y = abs(r2.w) * -0.0187292993 + 0.0742610022;
      r3.y = r3.y * abs(r2.w) + -0.212114394;
      r3.y = r3.y * abs(r2.w) + 1.57072878;
      r3.z = r3.y * r3.x;
      r3.z = r3.z * -2 + 3.14159274;
      r2.w = cmp(r2.w < -r2.w);
      r2.w = r2.w ? r3.z : 0;
      r2.w = r3.y * r3.x + r2.w;
      r3.x = cmp(0 < g_hdr_on);
      r3.y = ps_specular_params1[r0.x].y * ps_specular_params1[r0.x].y;
      r3.xz = r3.xx ? float2(0.00400000019,0.995899975) : float2(1,-0.000100016594);
      r3.x = r3.y * r3.z + r3.x;
      r3.x = r3.x * 0.5 + 0.5;
      r3.x = r3.x * 2 + -1;
      r3.y = -r3.x * r3.x + 1;
      r3.y = max(0.00100000005, r3.y);
      r3.y = log2(r3.y);
      r3.z = 4.95061684 * r3.y;
      r3.y = r3.y * 0.346573591 + 4.54688501;
      r3.w = cmp(0 < r3.x);
      r3.x = cmp(r3.x < 0);
      r3.x = (int)-r3.w + (int)r3.x;
      r3.x = (int)r3.x;
      r3.z = r3.y * r3.y + -r3.z;
      r3.z = sqrt(r3.z);
      r3.y = r3.z + -r3.y;
      r3.y = max(0, r3.y);
      r3.y = sqrt(r3.y);
      r3.x = r3.x * r3.y;
      r3.x = 1.41421354 * r3.x;
      r3.x = 0.00872664619 / r3.x;
      r3.x = max(9.99999975e-005, r3.x);
      r3.yz = float2(-0.00872664619,0.00872664619) + r2.ww;
      r2.w = 1 / r3.x;
      r3.xy = r3.yz * r2.ww;
      r3.zw = float2(0.707106769,0.707106769) * r3.xy;
      r3.zw = r3.zw * r3.zw;
      r5.xyzw = r3.zzww * float4(0.140012279,0.140012279,0.140012279,0.140012279) + float4(1.27323949,1,1.27323949,1);
      r5.xy = r5.xz / r5.yw;
      r3.zw = r5.xy * -r3.zw;
      r5.xy = cmp(float2(0,0) < r3.xy);
      r2.w = cmp(r3.x < 0);
      r2.w = (int)-r5.x + (int)r2.w;
      r2.w = (int)r2.w;
      r3.xy = float2(1.44269502,1.44269502) * r3.zw;
      r3.xy = exp2(r3.xy);
      r3.xy = float2(1,1) + -r3.xy;
      r3.xy = sqrt(r3.xy);
      r2.w = r2.w * r3.x + 1;
      r2.w = 0.5 * r2.w;
      r3.x = (int)-r5.y;
      r3.x = r3.x * r3.y + 1;
      r2.w = r3.x * 0.5 + -r2.w;
      r0.w = saturate(r0.w);
      r3.xy = float2(1.57079637,1.53938043) * r0.zz;
      r3.x = sin(r3.x);
      r0.w = -1 + r0.w;
      r0.w = r3.x * r0.w + 1;
      r3.x = dot(-sun_direction.xyz, -sun_direction.xyz);
      r3.x = log2(r3.x);
      r3.x = 10 * r3.x;
      r3.x = exp2(r3.x);
      r3.y = cos(r3.y);
      r3.y = rsqrt(r3.y);
      r3.y = 1 / r3.y;
      r3.z = saturate(ps_specular_params1[r0.x].x * 60);
      r3.x = r3.x * r3.y;
      r3.y = -ps_specular_params1[r0.x].x + r3.z;
      r3.x = r3.x * r3.y + ps_specular_params1[r0.x].x;
      r0.w = abs(r2.w) * r0.w;
      r0.w = r0.w * r3.x;
    } else {
      r0.w = 0;
    }
    r3.xyz = ps_specular_params[r0.x].xyz * r0.www;
    r3.xyz = sun_colour.xyz * r3.xyz;
    r3.xyz = ps_specular_params1[r0.x].zzz * r3.xyz;
    r3.xyz = r3.xyz * r4.www;
    r3.xyz *= VfxSpecularBrightness;
    r3.xyz = r4.xyz * r0.yyy + r3.xyz;
    r5.xyz = s_environment.SampleLevel(s_environment_s, r2.xyz, r0.z).xyz;
    r0.y = dot(r2.xyz, -r1.xyz);
    r0.y = max(0, r0.y);
    r0.y = log2(r0.y);
    r0.yz = float2(10,1.53938043) * r0.yz;
    r0.y = exp2(r0.y);
    r0.z = cos(r0.z);
    r0.z = rsqrt(r0.z);
    r0.z = 1 / r0.z;
    r0.w = saturate(ps_specular_params1[r0.x].x * 60);
    r0.y = r0.y * r0.z;
    r0.z = -ps_specular_params1[r0.x].x + r0.w;
    r0.y = r0.y * r0.z + ps_specular_params1[r0.x].x;
    r0.y = max(ps_specular_params1[r0.x].x, r0.y);
    r0.yzw = r5.xyz * r0.yyy;
    r0.yzw = ps_specular_params[r0.x].xyz * r0.yzw;
    r0.yzw *= VfxReflectionBrightness;
    r4.xyz = r0.yzw * r4.www + r3.xyz;
  }
  if (r1.w != 0) {
    r0.y = dot(r8.xyzw, view._m02_m12_m22_m32);
    r0.zw = g_vpos_texel_offset + v0.xy;
    r0.zw = g_render_target_dimensions.zw * r0.zw;
    r1.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, 0).x;
    r1.xy = r0.zw * float2(2,-2) + float2(-1,1);
    r1.w = 1;
    r0.z = dot(r1.xyzw, inv_projection._m02_m12_m22_m32);
    r0.w = dot(r1.xyzw, inv_projection._m03_m13_m23_m33);
    r0.z = r0.z / r0.w;
    r0.y = r0.z + -r0.y;
    r0.y = saturate(r0.y / ps_soft_edge_params[r0.x].x);
    if (VfxSoftParticleStrength != 1.0f) {
      r0.y = lerp(1.0f, r0.y, VfxSoftParticleStrength);
    }
    r4.xyzw = r4.xyzw * r0.yyyy;
  }
  r4.xyzw *= VfxOpacity;
  r4.xyz *= VfxOverallBrightness;
  r0.x = 0x01000000 & (int)behaviour_mask[r0.x].x;
  r1.xyz = -camera_position.xyz + v4.xyz;
  r1.w = max(0, r1.y);
  r0.yzw = s_sky.Sample(s_sky_s, r1.xwz).xyz;
  if (r0.x != 0) {
    r0.x = dot(r1.xyz, r1.xyz);
    r0.x = sqrt(r0.x);
    r1.y = saturate(g_fog_distance_start);
    r1.y = 1 + -r1.y;
    r1.y = r1.y * 8 + -4;
    r1.w = saturate(g_fog_distance_strength);
    r1.w = 1 + -r1.w;
    r1.w = 1000 * r1.w;
    r0.x = r0.x / r1.w;
    r0.x = r1.y + r0.x;
    r0.x = 1.44269502 * r0.x;
    r0.x = exp2(r0.x);
    r0.x = g_fog_distance_scale / r0.x;
    r0.x = saturate(g_fog_distance_scale + -r0.x);
    r1.y = g_fog_height_top + -v4.y;
    r1.w = g_fog_height_top + -g_fog_height_bottom;
    r1.y = -g_fog_height_bottom + r1.y;
    r1.w = 1 / r1.w;
    r1.y = saturate(r1.y * r1.w);
    r1.w = r1.y * -2 + 3;
    r1.y = r1.y * r1.y;
    r1.x = dot(r1.xz, r1.xz);
    r1.x = sqrt(r1.x);
    r1.z = max(0.00100000005, g_fog_clear_distance);
    r1.z = 1 / r1.z;
    r1.xy = r1.xw * r1.zy;
    r1.x = min(1, r1.x);
    r1.z = r1.x * -2 + 3;
    r1.x = r1.x * r1.x;
    r1.x = r1.z * r1.x;
    r0.x = g_fog_height_strength * r1.y + r0.x;
    r1.yzw = g_volume_fog_colour.xyz * sun_colour.xyz;
    r1.yzw = float3(1.5,1.5,1.5) * r1.yzw;
    r1.yzw = abs(sun_direction.y) * r1.yzw;
    r2.x = cmp(0 < g_hdr_on);
    r2.x = r2.x ? 0.00400000019 : 1;
    r2.yzw = r2.xxx * r1.yzw;
    r3.x = saturate(dot(r0.xx, g_fog_colour_blend));
    r0.yzw = -r1.yzw * r2.xxx + r0.yzw;
    r0.yzw = r3.xxx * r0.yzw + r2.yzw;
    r0.x = saturate(r1.x * r0.x);
    r0.x = saturate(r0.x * VfxFogAmount);
    r0.yzw *= VfxFogBrightness;
    r0.yzw = r0.yzw * r4.www + -r4.xyz;
    r4.xyz = r0.xxx * r0.yzw + r4.xyz;
  }
  o0.xyzw = r4.xyzw;
  return;
}