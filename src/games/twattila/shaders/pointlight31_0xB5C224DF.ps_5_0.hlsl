// 0xB5C224DF (pointlightlinearfalloffshadow slm lowdef, gather 5.0: the game's point light shadows on, Cutscene Point
// Light Shadows): deferred point lights, instanced (the light's position and reach per instance: POSITION2, v3). The
// game's bytes draw unless Rays Replace Point Light Shadows binds the traced lights (twattila/rt_scene.hpp
// InstallPointShadowLights; the game's shadow map read lit there): then x the light's traced visibility.
// 3Dmigoto decompile, its gather offsets from the disassembly, its temporaries precise and each x * y + z a mad (the
// game's instructions: bit for bit on WARP, also below 100% against view_share's patch; tools/tw/testbin/g4_point_shadows).
#include "../render_scale.hlsli"
#include "../rt_local_lights.hlsli"

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

cbuffer lighting_VS_PS : register(b1)
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

cbuffer point_light_PS : register(b2)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

cbuffer pointlight_shadowmap_cb : register(b3)
{
  float4 g_shadow_buffer_size : packoffset(c0);
  float g_shadow_sample_bias : packoffset(c1);
  float g_shadow_soft_edges : packoffset(c1.y);
  float2 g_light_proj_zw : packoffset(c1.z);
  float g_light_width : packoffset(c2);
  float g_point_light_shadow_map_sample_bias : packoffset(c2.y);
}

SamplerState s_ssao_s : register(s0);
SamplerState gbuffer_channel_1_sampler_s : register(s1);
SamplerState gbuffer_channel_2_sampler_s : register(s2);
SamplerState gbuffer_channel_3_sampler_s : register(s3);
SamplerState gbuffer_channel_4_sampler_s : register(s4);
SamplerState s_point_light_shadow_map_s : register(s6);
SamplerComparisonState s_point_light_shadow_map_sm50_s : register(s5);
Texture2D<float4> gbuffer_channel_1_sampler : register(t0);
Texture2D<float4> gbuffer_channel_2_sampler : register(t1);
Texture2D<float4> gbuffer_channel_3_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);
Texture2D<float4> s_ssao : register(t4);
Texture2DArray<float> t_point_light_shadow_map : register(t5);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0} };
  precise float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 != g_shadow_soft_edges);
  r0.yz = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r0.yz;
  r2.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r1.xy, r1.y).xyzw;
  r3.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r1.xy, r1.y).xyzw;
  r4.xyz = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r1.xy, r1.y).xyz;
  r5.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, r1.y).x;
  r3.xyz = r3.xyz * r3.xyz;
  r2.xyz = mad(r2.xyz, float3(2,2,2), float3(-1,-1,-1));
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r2.xyz * r0.www;
  // The AO blur draws its whole target below 100% (view_share.hpp RegionRead::whole).
  r0.w = s_ssao.Sample(s_ssao_s, r1.xy / DrawRenderShare()).x;
  r0.yz = mad(r0.yz, g_screen_size.zw, -g_viewport_offset_scale.xy);
  r0.yz = r0.yz / g_viewport_offset_scale.zw;
  // Below 100% the view fills its share of the target (view_share.hpp patches the game's own).
  r5.xy = mad((r0.yz / DrawRenderShare()), float2(2,-2), float2(-1,1));
  r5.w = 1;
  r1.x = dot(r5.xyzw, inv_view_projection._m00_m10_m20_m30);
  r1.y = dot(r5.xyzw, inv_view_projection._m01_m11_m21_m31);
  r1.z = dot(r5.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r5.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r1.xyz / r0.yyy;
  r5.xyz = -v3.xyz + r1.xyz;
  r6.xyz = g_point_light_shadow_map_sample_bias * r5.xyz;
  r0.y = max(abs(r6.x), abs(r6.y));
  r0.y = max(r0.y, abs(r6.z));
  r0.y = g_light_proj_zw.y / r0.y;
  r0.y = g_light_proj_zw.x + r0.y;
  r0.z = dot(r6.xyz, r6.xyz);
  r0.z = rsqrt(r0.z);
  r6.xyz = r6.xyz * r0.zzz;
  r7.xzw = -r6.zxy;
  r8.xyzw = cmp(abs(r6.yzxz) < abs(r6.xxyy));
  r9.xyz = cmp(float3(0,0,0) < r6.xyz);
  r7.y = 0;
  r10.xw = float2(1,2);
  r10.yz = float2(1,-1) * r6.zy;
  r11.xzw = r9.xxx ? r7.yxw : r10.xyz;
  r8.xy = r8.yw ? r8.xz : 0;
  r9.x = 3;
  r9.w = r7.x;
  r10.xw = r9.yy ? r10.wy : r9.xw;
  r8.zw = cmp(abs(r6.yx) < abs(r6.zz));
  r0.z = r8.w ? r8.z : 0;
  r12.x = 4;
  r12.zw = float2(1,-1) * r6.xy;
  r7.x = 5;
  r7.xzw = r9.zzz ? r12.xzw : r7.xzw;
  r7.y = abs(r6.z);
  r7.xyzw = r0.zzzz ? r7.xyzw : float4(0,1,0,0);
  r10.y = abs(r6.y);
  r10.z = r12.z;
  r7.xyzw = r8.yyyy ? r10.xyzw : r7.xyzw;
  r11.y = abs(r6.x);
  r6.xyzw = r8.xxxx ? r11.xyzw : r7.xyzw;
  r6.yz = r6.zw / r6.yy;
  r6.yz = float2(1,1) + r6.yz;
  r7.xy = float2(0.5,0.5) * r6.yz;
  r6.w = (int)r6.x;
  r0.z = cmp((int)r0.x == -1);
  if (r0.z != 0) {
    r8.xy = mad(g_shadow_buffer_size.xy, r7.xy, float2(0.5,0.5));
    r8.zw = floor(r8.xy);
    r8.xy = r8.xy + -r8.zw;
    r9.xy = mad(-r8.xy, g_shadow_buffer_size.zw, r7.xy);
    r10.y = -3 * g_shadow_buffer_size.z;
    r8.zw = float2(0,0);
    r0.z = -3;
    while (true) {
      r1.w = cmp(3 < (int)r0.z);
      if (r1.w != 0) break;
      r1.w = (int)r0.z;
      r10.z = g_shadow_buffer_size.w * r1.w;
      r6.yz = r10.yz + r9.xy;
      r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.yzw).xyzw;
      r12.xyzw = cmp(r11.xyzw >= r0.yyyy);
      r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r1.w = dot(r12.xyzw, float4(1,1,1,1));
      r4.w = dot(r11.xyzw, r12.xyzw);
      r6.x = mad(g_shadow_buffer_size.z, 2, r6.y);
      r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xzw).xyzw;
      r12.xyzw = cmp(r11.xyzw >= r0.yyyy);
      r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r12.xyzw, float4(1,1,1,1));
      r1.w = r5.w + r1.w;
      r5.w = dot(r11.xyzw, r12.xyzw);
      r4.w = r5.w + r4.w;
      r11.y = mad(g_shadow_buffer_size.z, 2, r6.x);
      r11.zw = r6.zw;
      r12.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r11.yzw).xyzw;
      r13.xyzw = cmp(r12.xyzw >= r0.yyyy);
      r13.xyzw = r13.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r13.xyzw, float4(1,1,1,1));
      r1.w = r5.w + r1.w;
      r5.w = dot(r12.xyzw, r13.xyzw);
      r4.w = r5.w + r4.w;
      r11.x = mad(g_shadow_buffer_size.z, 2, r11.y);
      r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r11.xzw).xyzw;
      r12.xyzw = cmp(r11.xyzw >= r0.yyyy);
      r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r12.xyzw, float4(1,1,1,1));
      r1.w = r5.w + r1.w;
      r5.w = dot(r11.xyzw, r12.xyzw);
      r4.w = r5.w + r4.w;
      r8.w = r8.w + r1.w;
      r8.z = r8.z + r4.w;
      r0.z = (int)r0.z + 2;
    }
    r0.z = cmp(r8.w < 64);
    if (r0.z != 0) {
      r0.z = cmp(0 < r8.w);
      r1.w = r8.z / r8.w;
      r1.w = r0.y / r1.w;
      r1.w = -1 + r1.w;
      r6.y = saturate(g_light_width * r1.w);
      r6.x = 0;
      r6.xy = r0.zz ? r6.xy : float2(1,0);
      if (r0.z != 0) {
        r0.z = r6.y * r6.y;
        r1.w = r0.z * r6.y;
        r4.w = 3 * r0.z;
        r5.w = 1 + -r6.y;
        r6.z = r5.w * r4.w;
        r7.w = r5.w * r5.w;
        r8.z = r7.w * r6.y;
        r8.w = 3 * r8.z;
        r9.w = r7.w * r5.w;
        r10.xy = float2(0,0);
        while (true) {
          r10.z = cmp((int)r10.x >= 11);
          if (r10.z != 0) break;
          r10.z = mad(r0.z, r6.y, r10.y);
          r10.w = (int)r10.x * 11;
          r11.x = mad(r6.z, icb[r10.w+1].x, r1.w);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+2].x * r6.z;
          r11.x = mad(r8.w, icb[r10.w+2].y, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+3].x * r6.z;
          r11.x = mad(r8.w, icb[r10.w+3].y, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+4].y * r8.w;
          r11.x = mad(r9.w, icb[r10.w+4].z, r11.x);
          r11.x = mad(r6.z, icb[r10.w+4].x, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+5].y * r8.w;
          r11.x = mad(r9.w, icb[r10.w+5].z, r11.x);
          r11.x = mad(r6.z, icb[r10.w+5].x, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+6].y * r8.w;
          r11.x = mad(r9.w, icb[r10.w+6].z, r11.x);
          r11.x = mad(r6.z, icb[r10.w+6].x, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+7].x * r6.z;
          r11.x = mad(r8.w, icb[r10.w+7].y, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r11.x = icb[r10.w+8].x * r6.z;
          r11.x = mad(r8.w, icb[r10.w+8].y, r11.x);
          r11.x = mad(r0.z, r6.y, r11.x);
          r10.z = r11.x + r10.z;
          r10.w = mad(r6.z, icb[r10.w+9].x, r1.w);
          r10.z = r10.z + r10.w;
          r10.y = mad(r0.z, r6.y, r10.z);
          r10.x = (int)r10.x + 1;
        }
        r9.z = r6.w;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, -5)).xyzw;
        r10.x = 1 + -r8.y;
        r10.z = mad(-r1.w, r8.x, r1.w);
        r11.xw = r11.wx * r10.zz;
        r11.xz = mad(r11.zy, r1.ww, r11.xw);
        r10.w = r11.z * r8.y;
        r10.w = mad(r10.x, r11.x, r10.w);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, -5)).xyzw;
        r11.x = dot(r12.wz, r1.ww);
        r10.w = mad(r10.x, r11.x, r10.w);
        r11.x = dot(r12.xy, r1.ww);
        r10.w = mad(r8.y, r11.x, r10.w);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, -5)).xyzw;
        r11.x = dot(r13.wz, r1.ww);
        r10.w = mad(r10.x, r11.x, r10.w);
        r11.x = dot(r13.xy, r1.ww);
        r10.w = mad(r8.y, r11.x, r10.w);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, -5)).xyzw;
        r11.x = dot(r14.wz, r1.ww);
        r10.w = mad(r10.x, r11.x, r10.w);
        r11.x = dot(r14.xy, r1.ww);
        r10.w = mad(r8.y, r11.x, r10.w);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, -5)).xyzw;
        r11.x = dot(r15.wz, r1.ww);
        r10.w = mad(r10.x, r11.x, r10.w);
        r11.x = dot(r15.xy, r1.ww);
        r10.w = mad(r8.y, r11.x, r10.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, -5)).xyzw;
        r11.xz = r16.zy * r8.xx;
        r11.xz = r11.xz * r1.ww;
        r12.zw = mad(r16.wx, r1.ww, r11.xz);
        r10.w = mad(r10.x, r12.z, r10.w);
        r10.w = mad(r8.y, r12.w, r10.w);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, -3)).xyzw;
        r11.x = 0.200000003 * r6.z;
        r12.z = mad(r6.z, 0.200000003, r1.w);
        r12.w = mad(r0.z, r6.y, -r12.z);
        r12.w = mad(r8.x, r12.w, r12.z);
        r13.zw = r17.yz * r12.ww;
        r13.zw = mad(r17.xw, r10.zz, r13.zw);
        r10.w = mad(r10.x, r13.w, r10.w);
        r10.w = mad(r8.y, r13.z, r10.w);
        r11.y = mad(r11.y, r12.w, r11.w);
        r10.w = mad(r10.x, r11.y, r10.w);
        r10.w = mad(r8.y, r13.w, r10.w);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, -3)).xyzw;
        r11.yw = mad(r8.zz, float2(3,0.600000024), r6.zz);
        r11.yw = mad(r0.zz, r6.yy, r11.yw);
        r6.z = r12.z + -r11.w;
        r6.z = mad(r8.x, r6.z, r11.w);
        r14.zw = r17.wx * r6.zz;
        r15.zw = mad(r17.zy, r11.ww, r14.zw);
        r8.z = mad(r10.x, r15.z, r10.w);
        r8.z = mad(r8.y, r15.w, r8.z);
        r10.w = dot(r12.xy, r12.zz);
        r8.z = mad(r10.x, r10.w, r8.z);
        r10.w = dot(r17.wz, r12.zz);
        r8.z = mad(r8.y, r10.w, r8.z);
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, -3)).xyzw;
        r10.w = dot(r18.wz, r11.ww);
        r8.z = mad(r10.x, r10.w, r8.z);
        r10.w = dot(r18.xy, r11.ww);
        r8.z = mad(r8.y, r10.w, r8.z);
        r10.w = dot(r13.xy, r12.zz);
        r8.z = mad(r10.x, r10.w, r8.z);
        r10.w = dot(r18.wz, r12.zz);
        r8.z = mad(r8.y, r10.w, r8.z);
        r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, -3)).xyzw;
        r10.w = dot(r19.wz, r11.ww);
        r8.z = mad(r10.x, r10.w, r8.z);
        r10.w = dot(r19.xy, r11.ww);
        r8.z = mad(r8.y, r10.w, r8.z);
        r10.w = dot(r14.xy, r12.zz);
        r8.z = mad(r10.x, r10.w, r8.z);
        r10.w = dot(r19.wz, r12.zz);
        r8.z = mad(r8.y, r10.w, r8.z);
        r20.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, -3)).xyzw;
        r10.w = r11.w + -r12.z;
        r10.w = mad(r8.x, r10.w, r12.z);
        r12.xy = r20.zy * r10.ww;
        r13.xy = mad(r20.wx, r11.ww, r12.xy);
        r8.z = mad(r10.x, r13.x, r8.z);
        r8.z = mad(r8.y, r13.y, r8.z);
        r12.x = dot(r15.xy, r12.zz);
        r8.z = mad(r10.x, r12.x, r8.z);
        r12.x = dot(r20.wz, r12.zz);
        r8.z = mad(r8.y, r12.x, r8.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, -3)).xyzw;
        r11.x = mad(r8.x, r11.x, r1.w);
        r13.xy = r15.yz * r8.xx;
        r13.xy = r13.xy * r1.ww;
        r13.xy = mad(r15.xw, r11.xx, r13.xy);
        r8.z = mad(r10.x, r13.y, r8.z);
        r8.z = mad(r8.y, r13.x, r8.z);
        r11.z = mad(r16.x, r11.x, r11.z);
        r8.z = mad(r10.x, r11.z, r8.z);
        r8.z = mad(r8.y, r13.y, r8.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, -1)).xyzw;
        r13.yw = r15.yz * r12.ww;
        r13.yw = mad(r15.xw, r10.zz, r13.yw);
        r8.z = mad(r10.x, r13.w, r8.z);
        r8.z = mad(r8.y, r13.y, r8.z);
        r8.z = mad(r10.x, r13.z, r8.z);
        r8.z = mad(r8.y, r13.w, r8.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, -1)).xyzw;
        r13.zw = r11.wy + -r11.yw;
        r13.zw = mad(r8.xx, r13.zw, r11.yw);
        r14.xy = r15.yz * r13.zz;
        r14.xy = mad(r15.xw, r6.zz, r14.xy);
        r8.z = mad(r10.x, r14.y, r8.z);
        r8.z = mad(r8.y, r14.x, r8.z);
        r11.z = mad(r17.y, r13.z, r14.w);
        r8.z = mad(r10.x, r11.z, r8.z);
        r8.z = mad(r8.y, r14.y, r8.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, -1)).xyzw;
        r9.w = mad(r9.w, 0.800000012, r8.w);
        r9.w = mad(r4.w, r5.w, r9.w);
        r9.w = mad(r0.z, r6.y, r9.w);
        r11.z = r11.y + -r9.w;
        r11.z = mad(r8.x, r11.z, r9.w);
        r14.yz = r15.wx * r11.zz;
        r14.yw = mad(r15.zy, r9.ww, r14.yz);
        r8.z = mad(r10.x, r14.y, r8.z);
        r8.z = mad(r8.y, r14.w, r8.z);
        r12.x = dot(r18.xy, r11.yy);
        r8.z = mad(r10.x, r12.x, r8.z);
        r12.x = dot(r15.wz, r11.yy);
        r8.z = mad(r8.y, r12.x, r8.z);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, -1)).xyzw;
        r12.x = r9.w + -r11.y;
        r12.x = mad(r8.x, r12.x, r11.y);
        r14.yw = r16.zy * r12.xx;
        r15.xz = mad(r16.wx, r9.ww, r14.yw);
        r8.z = mad(r10.x, r15.x, r8.z);
        r8.z = mad(r8.y, r15.z, r8.z);
        r14.y = dot(r19.xy, r11.yy);
        r8.z = mad(r10.x, r14.y, r8.z);
        r14.y = dot(r16.wz, r11.yy);
        r8.z = mad(r8.y, r14.y, r8.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, -1)).xyzw;
        r15.xz = r17.yz * r10.ww;
        r15.xz = mad(r17.xw, r13.ww, r15.xz);
        r8.z = mad(r10.x, r15.z, r8.z);
        r8.z = mad(r8.y, r15.x, r8.z);
        r12.y = mad(r20.x, r13.w, r12.y);
        r8.z = mad(r10.x, r12.y, r8.z);
        r8.z = mad(r8.y, r15.z, r8.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, -1)).xyzw;
        r15.zw = r17.yz * r8.xx;
        r15.zw = r15.zw * r1.ww;
        r15.zw = mad(r17.xw, r11.xx, r15.zw);
        r8.z = mad(r10.x, r15.w, r8.z);
        r8.z = mad(r8.y, r15.z, r8.z);
        r8.z = mad(r10.x, r13.x, r8.z);
        r8.z = mad(r8.y, r15.w, r8.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, 1)).xyzw;
        r16.yz = r17.yz * r12.ww;
        r16.yz = mad(r17.xw, r10.zz, r16.yz);
        r8.z = mad(r10.x, r16.z, r8.z);
        r8.z = mad(r8.y, r16.y, r8.z);
        r8.z = mad(r10.x, r13.y, r8.z);
        r8.z = mad(r8.y, r16.z, r8.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, 1)).xyzw;
        r13.xy = r17.yz * r13.zz;
        r13.xy = mad(r17.xw, r6.zz, r13.xy);
        r8.z = mad(r10.x, r13.y, r8.z);
        r8.z = mad(r8.y, r13.x, r8.z);
        r8.z = mad(r10.x, r14.x, r8.z);
        r8.z = mad(r8.y, r13.y, r8.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, 1)).xyzw;
        r14.xy = r17.wx * r11.zz;
        r16.zw = mad(r17.zy, r9.ww, r14.xy);
        r8.z = mad(r10.x, r16.z, r8.z);
        r8.z = mad(r8.y, r16.w, r8.z);
        r7.w = mad(r7.w, r5.w, r8.w);
        r4.w = mad(r4.w, r5.w, r7.w);
        r0.z = mad(r0.z, r6.y, r4.w);
        r4.w = r9.w + -r0.z;
        r4.w = mad(r8.x, r4.w, r0.z);
        r5.w = mad(r15.y, r4.w, r14.z);
        r5.w = mad(r10.x, r5.w, r8.z);
        r4.w = mad(r17.z, r4.w, r14.x);
        r4.w = mad(r8.y, r4.w, r5.w);
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, 1)).xyzw;
        r8.zw = r18.zy * r12.xx;
        r12.xy = mad(r18.wx, r9.ww, r8.zw);
        r4.w = mad(r10.x, r12.x, r4.w);
        r4.w = mad(r8.y, r12.y, r4.w);
        r0.z = r0.z + -r9.w;
        r0.z = mad(r8.x, r0.z, r9.w);
        r5.w = mad(r16.x, r0.z, r14.w);
        r4.w = mad(r10.x, r5.w, r4.w);
        r0.z = mad(r18.w, r0.z, r8.z);
        r0.z = mad(r8.y, r0.z, r4.w);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, 1)).xyzw;
        r8.zw = r14.yz * r10.ww;
        r8.zw = mad(r14.xw, r13.ww, r8.zw);
        r0.z = mad(r10.x, r8.w, r0.z);
        r0.z = mad(r8.y, r8.z, r0.z);
        r0.z = mad(r10.x, r15.x, r0.z);
        r0.z = mad(r8.y, r8.w, r0.z);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, 1)).xyzw;
        r12.xy = r14.yz * r8.xx;
        r12.xy = r12.xy * r1.ww;
        r12.xy = mad(r14.xw, r11.xx, r12.xy);
        r0.z = mad(r10.x, r12.y, r0.z);
        r0.z = mad(r8.y, r12.x, r0.z);
        r0.z = mad(r10.x, r15.z, r0.z);
        r0.z = mad(r8.y, r12.y, r0.z);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, 3)).xyzw;
        r14.yz = r14.yz * r12.ww;
        r14.xy = mad(r14.xw, r10.zz, r14.yz);
        r0.z = mad(r10.x, r14.y, r0.z);
        r0.z = mad(r8.y, r14.x, r0.z);
        r0.z = mad(r10.x, r16.y, r0.z);
        r0.z = mad(r8.y, r14.y, r0.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, 3)).xyzw;
        r6.yz = r15.wx * r6.zz;
        r14.yz = mad(r15.zy, r11.ww, r6.yz);
        r0.z = mad(r10.x, r14.y, r0.z);
        r0.z = mad(r8.y, r14.z, r0.z);
        r0.z = mad(r10.x, r13.x, r0.z);
        r4.w = mad(r15.z, r13.z, r6.y);
        r0.z = mad(r8.y, r4.w, r0.z);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, 3)).xyzw;
        r4.w = dot(r16.wz, r11.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r16.xy, r11.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r17.xy, r11.yy);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r16.wz, r11.yy);
        r0.z = mad(r8.y, r4.w, r0.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, 3)).xyzw;
        r4.w = dot(r17.wz, r11.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r17.xy, r11.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r18.xy, r11.yy);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r17.wz, r11.yy);
        r0.z = mad(r8.y, r4.w, r0.z);
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, 3)).xyzw;
        r6.yz = r18.zy * r10.ww;
        r11.yz = mad(r18.wx, r11.ww, r6.yz);
        r0.z = mad(r10.x, r11.y, r0.z);
        r0.z = mad(r8.y, r11.z, r0.z);
        r0.z = mad(r10.x, r8.z, r0.z);
        r4.w = mad(r18.w, r13.w, r6.y);
        r0.z = mad(r8.y, r4.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, 3)).xyzw;
        r6.yz = r13.yz * r8.xx;
        r6.yz = r6.yz * r1.ww;
        r6.yz = mad(r13.xw, r11.xx, r6.yz);
        r0.z = mad(r10.x, r6.z, r0.z);
        r0.z = mad(r8.y, r6.y, r0.z);
        r0.z = mad(r10.x, r12.x, r0.z);
        r0.z = mad(r8.y, r6.z, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-5, 5)).xyzw;
        r8.zw = r13.wx * r10.zz;
        r10.zw = mad(r13.zy, r1.ww, r8.zw);
        r0.z = mad(r10.x, r10.z, r0.z);
        r0.z = mad(r8.y, r10.w, r0.z);
        r0.z = mad(r10.x, r14.x, r0.z);
        r4.w = mad(r13.z, r12.w, r8.z);
        r0.z = mad(r8.y, r4.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-3, 5)).xyzw;
        r4.w = dot(r13.wz, r1.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.xy, r1.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r15.xy, r12.zz);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.wz, r12.zz);
        r0.z = mad(r8.y, r4.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(-1, 5)).xyzw;
        r4.w = dot(r13.wz, r1.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.xy, r1.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r16.xy, r12.zz);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.wz, r12.zz);
        r0.z = mad(r8.y, r4.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(1, 5)).xyzw;
        r4.w = dot(r13.wz, r1.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.xy, r1.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r17.xy, r12.zz);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.wz, r12.zz);
        r0.z = mad(r8.y, r4.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(3, 5)).xyzw;
        r4.w = dot(r13.wz, r1.ww);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.xy, r1.ww);
        r0.z = mad(r8.y, r4.w, r0.z);
        r4.w = dot(r18.xy, r12.zz);
        r0.z = mad(r10.x, r4.w, r0.z);
        r4.w = dot(r13.wz, r12.zz);
        r0.z = mad(r8.y, r4.w, r0.z);
        r9.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r9.xyz, r0.y, int2(5, 5)).xyzw;
        r8.xz = r9.zy * r8.xx;
        r8.xz = r8.xz * r1.ww;
        r8.zw = mad(r9.wx, r1.ww, r8.xz);
        r0.z = mad(r10.x, r8.z, r0.z);
        r0.z = mad(r8.y, r8.w, r0.z);
        r0.z = mad(r10.x, r6.y, r0.z);
        r1.w = mad(r9.w, r11.x, r8.x);
        r0.z = mad(r8.y, r1.w, r0.z);
        r6.x = r0.z / r10.y;
      }
    } else {
      r6.x = 0;
    }
  } else {
    r6.x = 1;
  }
  if (r0.x == 0) {
    r7.z = r6.w;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r7.xyz).x;
    r0.x = cmp(r0.y < r0.x);
    r6.x = r0.x ? 1.000000 : 0;
  }
  r0.x = dot(r5.xyz, r5.xyz);
  r0.y = rsqrt(r0.x);
  r5.xyz = r5.xyz * r0.yyy;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / v3.w);
  r0.x = 1 + -r0.x;
  r0.y = cmp(0 < g_hdr_on);
  r6.yz = r0.yy ? float2(0.00400000019,1) : float2(1,273);
  r0.z = dot(r2.xyz, -r5.xyz);
  r1.w = max(0, r0.z);
  r0.z = cmp(0 < r0.z);
  if (r0.z != 0) {
    r1.xyz = -camera_position.xyz + r1.xyz;
    r0.z = dot(r1.xyz, r1.xyz);
    r0.z = rsqrt(r0.z);
    r1.xyz = r1.xyz * r0.zzz;
    r0.z = 1 + -r2.w;
    r4.w = dot(r1.xyz, r2.xyz);
    r4.w = r4.w + r4.w;
    r2.xyz = mad(r2.xyz, -r4.www, r1.xyz);
    r2.x = dot(-r5.xyz, r2.xyz);
    r2.x = max(-1, r2.x);
    r2.x = min(1, r2.x);
    r2.y = 1 + -abs(r2.x);
    r2.y = sqrt(r2.y);
    r2.z = mad(abs(r2.x), -0.0187292993, 0.0742610022);
    r2.z = mad(r2.z, abs(r2.x), -0.212114394);
    r2.z = mad(r2.z, abs(r2.x), 1.57072878);
    r4.w = r2.z * r2.y;
    r4.w = mad(r4.w, -2, 3.14159274);
    r2.x = cmp(r2.x < -r2.x);
    r2.x = r2.x ? r4.w : 0;
    r2.x = mad(r2.z, r2.y, r2.x);
    r2.y = r2.w * r2.w;
    r0.y = r0.y ? 0.995899975 : -0.000100016594;
    r0.y = mad(r2.y, r0.y, r6.y);
    r0.y = mad(r0.y, 0.5, 0.5);
    r0.y = mad(r0.y, 2, -1);
    r2.y = mad(-r0.y, r0.y, 1);
    r2.y = max(0.00100000005, r2.y);
    r2.y = log2(r2.y);
    r2.z = 4.95061684 * r2.y;
    r2.y = mad(r2.y, 0.346573591, 4.54688501);
    r2.w = cmp(0 < r0.y);
    r0.y = cmp(r0.y < 0);
    r0.y = (int)-r2.w + (int)r0.y;
    r0.y = (int)r0.y;
    r2.z = mad(r2.y, r2.y, -r2.z);
    r2.z = sqrt(r2.z);
    r2.y = r2.z + -r2.y;
    r2.y = max(0, r2.y);
    r2.y = sqrt(r2.y);
    r0.y = r2.y * r0.y;
    r0.y = 1.41421354 * r0.y;
    r0.y = 0.00872664619 / r0.y;
    r0.y = max(9.99999975e-005, r0.y);
    r2.xy = float2(-0.00872664619,0.00872664619) + r2.xx;
    r0.y = 1 / r0.y;
    r2.xy = r2.xy * r0.yy;
    r2.zw = float2(0.707106769,0.707106769) * r2.xy;
    r2.zw = r2.zw * r2.zw;
    r7.xyzw = mad(r2.zzww, float4(0.140012279,0.140012279,0.140012279,0.140012279), float4(1.27323949,1,1.27323949,1));
    r7.xy = r7.xz / r7.yw;
    r2.zw = r7.xy * -r2.zw;
    r7.xy = cmp(float2(0,0) < r2.xy);
    r0.y = cmp(r2.x < 0);
    r0.y = (int)-r7.x + (int)r0.y;
    r0.y = (int)r0.y;
    r2.xy = float2(1.44269502,1.44269502) * r2.zw;
    r2.xy = exp2(r2.xy);
    r2.xy = float2(1,1) + -r2.xy;
    r2.xy = sqrt(r2.xy);
    r0.y = mad(r0.y, r2.x, 1);
    r0.y = 0.5 * r0.y;
    r2.x = (int)-r7.y;
    r2.x = mad(r2.x, r2.y, 1);
    r0.y = mad(r2.x, 0.5, -r0.y);
    r2.x = min(1, r1.w);
    r2.yz = float2(1.57079637,1.53938043) * r0.zz;
    r0.z = sin(r2.y);
    r2.x = -1 + r2.x;
    r0.z = mad(r0.z, r2.x, 1);
    r1.x = dot(-r5.xyz, r1.xyz);
    r1.x = max(0, r1.x);
    r1.x = log2(r1.x);
    r1.x = 10 * r1.x;
    r1.x = exp2(r1.x);
    r1.y = cos(r2.z);
    r1.y = rsqrt(r1.y);
    r1.y = 1 / r1.y;
    r1.z = saturate(60 * r3.w);
    r1.x = r1.x * r1.y;
    r1.y = r1.z + -r3.w;
    r1.x = mad(r1.x, r1.y, r3.w);
    r0.y = abs(r0.y) * r0.z;
    r1.xyz = r0.yyy * r1.xxx;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r2.xyz = saturate(r1.xyz * r6.zzz);
  r2.xyz = r2.xyz * r4.xyz;
  r1.xyz = max(r1.xyz, r3.www);
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r3.xyz = r3.xyz * r1.www;
  r3.xyz = v2.xyz * r3.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = r1.xyz * r6.yyy;
  r1.xyz = mad(r2.xyz, v2.xyz, r1.xyz);
  r1.xyz = r6.xxx * r1.xyz;
  r1.xyz = r1.xyz * r0.xxx;
  r1.w = 1;
  o0.xyzw = saturate(r1.xyzw * r0.wwww);
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}