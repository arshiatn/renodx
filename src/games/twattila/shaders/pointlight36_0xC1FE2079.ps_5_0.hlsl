// 0xC1FE2079 (pointlightsmoothfalloffshadow skinlm highdef, gather 5.0: the game's point light shadows on, Cutscene Point
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

SamplerState gbuffer_channel_1_sampler_s : register(s0);
SamplerState gbuffer_channel_2_sampler_s : register(s1);
SamplerState gbuffer_channel_3_sampler_s : register(s2);
SamplerState gbuffer_channel_4_sampler_s : register(s3);
SamplerState s_point_light_shadow_map_s : register(s5);
SamplerComparisonState s_point_light_shadow_map_sm50_s : register(s4);
Texture2D<float4> gbuffer_channel_1_sampler : register(t0);
Texture2D<float4> gbuffer_channel_2_sampler : register(t1);
Texture2D<float4> gbuffer_channel_3_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);
Texture2DArray<float> t_point_light_shadow_map : register(t4);


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
  r0.w = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r1.xy, r1.y).y;
  r1.z = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r1.xy, r1.y).x;
  r2.xyz = mad(r2.xyz, float3(2,2,2), float3(-1,-1,-1));
  r4.x = dot(r2.xyz, r2.xyz);
  r4.x = rsqrt(r4.x);
  r2.xyz = r4.xxx * r2.xyz;
  r0.yz = mad(r0.yz, g_screen_size.zw, -g_viewport_offset_scale.xy);
  r0.yz = r0.yz / g_viewport_offset_scale.zw;
  // Below 100% the view fills its share of the target (view_share.hpp patches the game's own).
  r1.xy = mad((r0.yz / DrawRenderShare()), float2(2,-2), float2(-1,1));
  r1.w = 1;
  r4.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r4.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r4.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r4.xyz / r0.yyy;
  r4.xyz = -v3.xyz + r1.xyz;
  r5.xyz = g_point_light_shadow_map_sample_bias * r4.xyz;
  r0.y = max(abs(r5.x), abs(r5.y));
  r0.y = max(r0.y, abs(r5.z));
  r0.y = g_light_proj_zw.y / r0.y;
  r0.y = g_light_proj_zw.x + r0.y;
  r0.z = dot(r5.xyz, r5.xyz);
  r0.z = rsqrt(r0.z);
  r5.xyz = r5.xyz * r0.zzz;
  r6.xzw = -r5.zxy;
  r7.xyzw = cmp(abs(r5.yzxz) < abs(r5.xxyy));
  r8.xyz = cmp(float3(0,0,0) < r5.xyz);
  r6.y = 0;
  r9.xw = float2(1,2);
  r9.yz = float2(1,-1) * r5.zy;
  r10.xzw = r8.xxx ? r6.yxw : r9.xyz;
  r7.xy = r7.yw ? r7.xz : 0;
  r8.x = 3;
  r8.w = r6.x;
  r9.xw = r8.yy ? r9.wy : r8.xw;
  r7.zw = cmp(abs(r5.yx) < abs(r5.zz));
  r0.z = r7.w ? r7.z : 0;
  r11.x = 4;
  r11.zw = float2(1,-1) * r5.xy;
  r6.x = 5;
  r6.xzw = r8.zzz ? r11.xzw : r6.xzw;
  r6.y = abs(r5.z);
  r6.xyzw = r0.zzzz ? r6.xyzw : float4(0,1,0,0);
  r9.y = abs(r5.y);
  r9.z = r11.z;
  r6.xyzw = r7.yyyy ? r9.xyzw : r6.xyzw;
  r10.y = abs(r5.x);
  r5.xyzw = r7.xxxx ? r10.xyzw : r6.xyzw;
  r5.yz = r5.zw / r5.yy;
  r5.yz = float2(1,1) + r5.yz;
  r6.xy = float2(0.5,0.5) * r5.yz;
  r5.w = (int)r5.x;
  r0.z = cmp((int)r0.x == -1);
  if (r0.z != 0) {
    r7.xy = mad(g_shadow_buffer_size.xy, r6.xy, float2(0.5,0.5));
    r7.zw = floor(r7.xy);
    r7.xy = r7.xy + -r7.zw;
    r8.xy = mad(-r7.xy, g_shadow_buffer_size.zw, r6.xy);
    r9.y = -3 * g_shadow_buffer_size.z;
    r7.zw = float2(0,0);
    r0.z = -3;
    while (true) {
      r1.w = cmp(3 < (int)r0.z);
      if (r1.w != 0) break;
      r1.w = (int)r0.z;
      r9.z = g_shadow_buffer_size.w * r1.w;
      r5.yz = r9.yz + r8.xy;
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.yzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.yyyy);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r1.w = dot(r11.xyzw, float4(1,1,1,1));
      r4.w = dot(r10.xyzw, r11.xyzw);
      r5.x = mad(g_shadow_buffer_size.z, 2, r5.y);
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.yyyy);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.y = dot(r11.xyzw, float4(1,1,1,1));
      r1.w = r5.y + r1.w;
      r5.y = dot(r10.xyzw, r11.xyzw);
      r4.w = r5.y + r4.w;
      r10.y = mad(g_shadow_buffer_size.z, 2, r5.x);
      r10.zw = r5.zw;
      r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.yzw).xyzw;
      r12.xyzw = cmp(r11.xyzw >= r0.yyyy);
      r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.x = dot(r12.xyzw, float4(1,1,1,1));
      r1.w = r5.x + r1.w;
      r5.x = dot(r11.xyzw, r12.xyzw);
      r4.w = r5.x + r4.w;
      r10.x = mad(g_shadow_buffer_size.z, 2, r10.y);
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.xzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.yyyy);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.x = dot(r11.xyzw, float4(1,1,1,1));
      r1.w = r5.x + r1.w;
      r5.x = dot(r10.xyzw, r11.xyzw);
      r4.w = r5.x + r4.w;
      r7.w = r7.w + r1.w;
      r7.z = r7.z + r4.w;
      r0.z = (int)r0.z + 2;
    }
    r0.z = cmp(r7.w < 64);
    if (r0.z != 0) {
      r0.z = cmp(0 < r7.w);
      r1.w = r7.z / r7.w;
      r1.w = r0.y / r1.w;
      r1.w = -1 + r1.w;
      r5.y = saturate(g_light_width * r1.w);
      r5.x = 0;
      r5.xy = r0.zz ? r5.xy : float2(1,0);
      if (r0.z != 0) {
        r0.z = r5.y * r5.y;
        r1.w = r0.z * r5.y;
        r4.w = 3 * r0.z;
        r5.z = 1 + -r5.y;
        r6.w = r5.z * r4.w;
        r7.z = r5.z * r5.z;
        r7.w = r7.z * r5.y;
        r8.w = 3 * r7.w;
        r9.x = r7.z * r5.z;
        r9.yz = float2(0,0);
        while (true) {
          r9.w = cmp((int)r9.y >= 11);
          if (r9.w != 0) break;
          r9.w = mad(r0.z, r5.y, r9.z);
          r10.x = (int)r9.y * 11;
          r10.y = mad(r6.w, icb[r10.x+1].x, r1.w);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+2].x * r6.w;
          r10.y = mad(r8.w, icb[r10.x+2].y, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+3].x * r6.w;
          r10.y = mad(r8.w, icb[r10.x+3].y, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+4].y * r8.w;
          r10.y = mad(r9.x, icb[r10.x+4].z, r10.y);
          r10.y = mad(r6.w, icb[r10.x+4].x, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+5].y * r8.w;
          r10.y = mad(r9.x, icb[r10.x+5].z, r10.y);
          r10.y = mad(r6.w, icb[r10.x+5].x, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+6].y * r8.w;
          r10.y = mad(r9.x, icb[r10.x+6].z, r10.y);
          r10.y = mad(r6.w, icb[r10.x+6].x, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+7].x * r6.w;
          r10.y = mad(r8.w, icb[r10.x+7].y, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.y = icb[r10.x+8].x * r6.w;
          r10.y = mad(r8.w, icb[r10.x+8].y, r10.y);
          r10.y = mad(r0.z, r5.y, r10.y);
          r9.w = r10.y + r9.w;
          r10.x = mad(r6.w, icb[r10.x+9].x, r1.w);
          r9.w = r10.x + r9.w;
          r9.z = mad(r0.z, r5.y, r9.w);
          r9.y = (int)r9.y + 1;
        }
        r8.z = r5.w;
        r10.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, -5)).xyzw;
        r9.y = 1 + -r7.y;
        r9.w = mad(-r1.w, r7.x, r1.w);
        r10.xw = r10.wx * r9.ww;
        r10.xz = mad(r10.zy, r1.ww, r10.xw);
        r10.z = r10.z * r7.y;
        r10.x = mad(r9.y, r10.x, r10.z);
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, -5)).xyzw;
        r10.z = dot(r11.wz, r1.ww);
        r10.x = mad(r9.y, r10.z, r10.x);
        r10.z = dot(r11.xy, r1.ww);
        r10.x = mad(r7.y, r10.z, r10.x);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, -5)).xyzw;
        r10.z = dot(r12.wz, r1.ww);
        r10.x = mad(r9.y, r10.z, r10.x);
        r10.z = dot(r12.xy, r1.ww);
        r10.x = mad(r7.y, r10.z, r10.x);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, -5)).xyzw;
        r10.z = dot(r13.wz, r1.ww);
        r10.x = mad(r9.y, r10.z, r10.x);
        r10.z = dot(r13.xy, r1.ww);
        r10.x = mad(r7.y, r10.z, r10.x);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, -5)).xyzw;
        r10.z = dot(r14.wz, r1.ww);
        r10.x = mad(r9.y, r10.z, r10.x);
        r10.z = dot(r14.xy, r1.ww);
        r10.x = mad(r7.y, r10.z, r10.x);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, -5)).xyzw;
        r11.zw = r15.zy * r7.xx;
        r11.zw = r11.zw * r1.ww;
        r12.zw = mad(r15.wx, r1.ww, r11.zw);
        r10.x = mad(r9.y, r12.z, r10.x);
        r10.x = mad(r7.y, r12.w, r10.x);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, -3)).xyzw;
        r10.z = 0.200000003 * r6.w;
        r11.z = mad(r6.w, 0.200000003, r1.w);
        r12.z = mad(r0.z, r5.y, -r11.z);
        r12.z = mad(r7.x, r12.z, r11.z);
        r13.zw = r16.yz * r12.zz;
        r13.zw = mad(r16.xw, r9.ww, r13.zw);
        r10.x = mad(r9.y, r13.w, r10.x);
        r10.x = mad(r7.y, r13.z, r10.x);
        r10.y = mad(r10.y, r12.z, r10.w);
        r10.x = mad(r9.y, r10.y, r10.x);
        r10.x = mad(r7.y, r13.w, r10.x);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, -3)).xyzw;
        r10.yw = mad(r7.ww, float2(3,0.600000024), r6.ww);
        r10.yw = mad(r0.zz, r5.yy, r10.yw);
        r6.w = r11.z + -r10.w;
        r6.w = mad(r7.x, r6.w, r10.w);
        r14.zw = r16.wx * r6.ww;
        r15.yz = mad(r16.zy, r10.ww, r14.zw);
        r7.w = mad(r9.y, r15.y, r10.x);
        r7.w = mad(r7.y, r15.z, r7.w);
        r10.x = dot(r11.xy, r11.zz);
        r7.w = mad(r9.y, r10.x, r7.w);
        r10.x = dot(r16.wz, r11.zz);
        r7.w = mad(r7.y, r10.x, r7.w);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, -3)).xyzw;
        r10.x = dot(r17.wz, r10.ww);
        r7.w = mad(r9.y, r10.x, r7.w);
        r10.x = dot(r17.xy, r10.ww);
        r7.w = mad(r7.y, r10.x, r7.w);
        r10.x = dot(r12.xy, r11.zz);
        r7.w = mad(r9.y, r10.x, r7.w);
        r10.x = dot(r17.wz, r11.zz);
        r7.w = mad(r7.y, r10.x, r7.w);
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, -3)).xyzw;
        r10.x = dot(r18.wz, r10.ww);
        r7.w = mad(r9.y, r10.x, r7.w);
        r10.x = dot(r18.xy, r10.ww);
        r7.w = mad(r7.y, r10.x, r7.w);
        r10.x = dot(r13.xy, r11.zz);
        r7.w = mad(r9.y, r10.x, r7.w);
        r10.x = dot(r18.wz, r11.zz);
        r7.w = mad(r7.y, r10.x, r7.w);
        r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, -3)).xyzw;
        r10.x = r10.w + -r11.z;
        r10.x = mad(r7.x, r10.x, r11.z);
        r11.xy = r19.zy * r10.xx;
        r12.xy = mad(r19.wx, r10.ww, r11.xy);
        r7.w = mad(r9.y, r12.x, r7.w);
        r7.w = mad(r7.y, r12.y, r7.w);
        r11.x = dot(r14.xy, r11.zz);
        r7.w = mad(r9.y, r11.x, r7.w);
        r11.x = dot(r19.wz, r11.zz);
        r7.w = mad(r7.y, r11.x, r7.w);
        r20.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, -3)).xyzw;
        r10.z = mad(r7.x, r10.z, r1.w);
        r12.xy = r20.yz * r7.xx;
        r12.xy = r12.xy * r1.ww;
        r12.xy = mad(r20.xw, r10.zz, r12.xy);
        r7.w = mad(r9.y, r12.y, r7.w);
        r7.w = mad(r7.y, r12.x, r7.w);
        r11.x = mad(r15.x, r10.z, r11.w);
        r7.w = mad(r9.y, r11.x, r7.w);
        r7.w = mad(r7.y, r12.y, r7.w);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, -1)).xyzw;
        r11.xw = r15.yz * r12.zz;
        r11.xw = mad(r15.xw, r9.ww, r11.xw);
        r7.w = mad(r9.y, r11.w, r7.w);
        r7.w = mad(r7.y, r11.x, r7.w);
        r7.w = mad(r9.y, r13.z, r7.w);
        r7.w = mad(r7.y, r11.w, r7.w);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, -1)).xyzw;
        r12.yw = r10.wy + -r10.yw;
        r12.yw = mad(r7.xx, r12.yw, r10.yw);
        r13.yz = r13.yz * r12.yy;
        r13.xy = mad(r13.xw, r6.ww, r13.yz);
        r7.w = mad(r9.y, r13.y, r7.w);
        r7.w = mad(r7.y, r13.x, r7.w);
        r11.w = mad(r16.y, r12.y, r14.w);
        r7.w = mad(r9.y, r11.w, r7.w);
        r7.w = mad(r7.y, r13.y, r7.w);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, -1)).xyzw;
        r9.x = mad(r9.x, 0.800000012, r8.w);
        r9.x = mad(r4.w, r5.z, r9.x);
        r9.x = mad(r0.z, r5.y, r9.x);
        r11.w = r10.y + -r9.x;
        r11.w = mad(r7.x, r11.w, r9.x);
        r13.yz = r14.wx * r11.ww;
        r13.yw = mad(r14.zy, r9.xx, r13.yz);
        r7.w = mad(r9.y, r13.y, r7.w);
        r7.w = mad(r7.y, r13.w, r7.w);
        r13.y = dot(r17.xy, r10.yy);
        r7.w = mad(r9.y, r13.y, r7.w);
        r13.y = dot(r14.wz, r10.yy);
        r7.w = mad(r7.y, r13.y, r7.w);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, -1)).xyzw;
        r13.y = r9.x + -r10.y;
        r13.y = mad(r7.x, r13.y, r10.y);
        r14.xz = r15.zy * r13.yy;
        r14.xw = mad(r15.wx, r9.xx, r14.xz);
        r7.w = mad(r9.y, r14.x, r7.w);
        r7.w = mad(r7.y, r14.w, r7.w);
        r13.w = dot(r18.xy, r10.yy);
        r7.w = mad(r9.y, r13.w, r7.w);
        r13.w = dot(r15.wz, r10.yy);
        r7.w = mad(r7.y, r13.w, r7.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, -1)).xyzw;
        r14.xw = r16.yz * r10.xx;
        r14.xw = mad(r16.xw, r12.ww, r14.xw);
        r7.w = mad(r9.y, r14.w, r7.w);
        r7.w = mad(r7.y, r14.x, r7.w);
        r11.y = mad(r19.x, r12.w, r11.y);
        r7.w = mad(r9.y, r11.y, r7.w);
        r7.w = mad(r7.y, r14.w, r7.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, -1)).xyzw;
        r15.yz = r16.yz * r7.xx;
        r15.yz = r15.yz * r1.ww;
        r15.yz = mad(r16.xw, r10.zz, r15.yz);
        r7.w = mad(r9.y, r15.z, r7.w);
        r7.w = mad(r7.y, r15.y, r7.w);
        r7.w = mad(r9.y, r12.x, r7.w);
        r7.w = mad(r7.y, r15.z, r7.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, 1)).xyzw;
        r15.zw = r16.yz * r12.zz;
        r15.zw = mad(r16.xw, r9.ww, r15.zw);
        r7.w = mad(r9.y, r15.w, r7.w);
        r7.w = mad(r7.y, r15.z, r7.w);
        r7.w = mad(r9.y, r11.x, r7.w);
        r7.w = mad(r7.y, r15.w, r7.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, 1)).xyzw;
        r11.xy = r16.yz * r12.yy;
        r11.xy = mad(r16.xw, r6.ww, r11.xy);
        r7.w = mad(r9.y, r11.y, r7.w);
        r7.w = mad(r7.y, r11.x, r7.w);
        r7.w = mad(r9.y, r13.x, r7.w);
        r7.w = mad(r7.y, r11.y, r7.w);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, 1)).xyzw;
        r11.yw = r16.wx * r11.ww;
        r13.xw = mad(r16.zy, r9.xx, r11.yw);
        r7.w = mad(r9.y, r13.x, r7.w);
        r7.w = mad(r7.y, r13.w, r7.w);
        r7.z = mad(r7.z, r5.z, r8.w);
        r4.w = mad(r4.w, r5.z, r7.z);
        r0.z = mad(r0.z, r5.y, r4.w);
        r4.w = r9.x + -r0.z;
        r4.w = mad(r7.x, r4.w, r0.z);
        r5.y = mad(r14.y, r4.w, r13.z);
        r5.y = mad(r9.y, r5.y, r7.w);
        r4.w = mad(r16.z, r4.w, r11.y);
        r4.w = mad(r7.y, r4.w, r5.y);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, 1)).xyzw;
        r5.yz = r17.zy * r13.yy;
        r7.zw = mad(r17.wx, r9.xx, r5.yz);
        r4.w = mad(r9.y, r7.z, r4.w);
        r4.w = mad(r7.y, r7.w, r4.w);
        r0.z = r0.z + -r9.x;
        r0.z = mad(r7.x, r0.z, r9.x);
        r5.z = mad(r15.x, r0.z, r14.z);
        r4.w = mad(r9.y, r5.z, r4.w);
        r0.z = mad(r17.w, r0.z, r5.y);
        r0.z = mad(r7.y, r0.z, r4.w);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, 1)).xyzw;
        r5.yz = r13.yz * r10.xx;
        r5.yz = mad(r13.xw, r12.ww, r5.yz);
        r0.z = mad(r9.y, r5.z, r0.z);
        r0.z = mad(r7.y, r5.y, r0.z);
        r0.z = mad(r9.y, r14.x, r0.z);
        r0.z = mad(r7.y, r5.z, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, 1)).xyzw;
        r7.zw = r13.yz * r7.xx;
        r7.zw = r7.zw * r1.ww;
        r7.zw = mad(r13.xw, r10.zz, r7.zw);
        r0.z = mad(r9.y, r7.w, r0.z);
        r0.z = mad(r7.y, r7.z, r0.z);
        r0.z = mad(r9.y, r15.y, r0.z);
        r0.z = mad(r7.y, r7.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, 3)).xyzw;
        r11.yw = r13.yz * r12.zz;
        r11.yw = mad(r13.xw, r9.ww, r11.yw);
        r0.z = mad(r9.y, r11.w, r0.z);
        r0.z = mad(r7.y, r11.y, r0.z);
        r0.z = mad(r9.y, r15.z, r0.z);
        r0.z = mad(r7.y, r11.w, r0.z);
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, 3)).xyzw;
        r14.xy = r13.wx * r6.ww;
        r14.yz = mad(r13.zy, r10.ww, r14.xy);
        r0.z = mad(r9.y, r14.y, r0.z);
        r0.z = mad(r7.y, r14.z, r0.z);
        r0.z = mad(r9.y, r11.x, r0.z);
        r4.w = mad(r13.z, r12.y, r14.x);
        r0.z = mad(r7.y, r4.w, r0.z);
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, 3)).xyzw;
        r4.w = dot(r14.wz, r10.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r14.xy, r10.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r16.xy, r10.yy);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r14.wz, r10.yy);
        r0.z = mad(r7.y, r4.w, r0.z);
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, 3)).xyzw;
        r4.w = dot(r15.wz, r10.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r15.xy, r10.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r17.xy, r10.yy);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r15.wz, r10.yy);
        r0.z = mad(r7.y, r4.w, r0.z);
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, 3)).xyzw;
        r10.xy = r16.zy * r10.xx;
        r10.yw = mad(r16.wx, r10.ww, r10.xy);
        r0.z = mad(r9.y, r10.y, r0.z);
        r0.z = mad(r7.y, r10.w, r0.z);
        r0.z = mad(r9.y, r5.y, r0.z);
        r4.w = mad(r16.w, r12.w, r10.x);
        r0.z = mad(r7.y, r4.w, r0.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, 3)).xyzw;
        r5.yz = r17.yz * r7.xx;
        r5.yz = r5.yz * r1.ww;
        r5.yz = mad(r17.xw, r10.zz, r5.yz);
        r0.z = mad(r9.y, r5.z, r0.z);
        r0.z = mad(r7.y, r5.y, r0.z);
        r0.z = mad(r9.y, r7.z, r0.z);
        r0.z = mad(r7.y, r5.z, r0.z);
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-5, 5)).xyzw;
        r7.zw = r17.wx * r9.ww;
        r9.xw = mad(r17.zy, r1.ww, r7.zw);
        r0.z = mad(r9.y, r9.x, r0.z);
        r0.z = mad(r7.y, r9.w, r0.z);
        r0.z = mad(r9.y, r11.y, r0.z);
        r4.w = mad(r17.z, r12.z, r7.z);
        r0.z = mad(r7.y, r4.w, r0.z);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-3, 5)).xyzw;
        r4.w = dot(r12.wz, r1.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.xy, r1.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r13.xy, r11.zz);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.wz, r11.zz);
        r0.z = mad(r7.y, r4.w, r0.z);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(-1, 5)).xyzw;
        r4.w = dot(r12.wz, r1.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.xy, r1.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r14.xy, r11.zz);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.wz, r11.zz);
        r0.z = mad(r7.y, r4.w, r0.z);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(1, 5)).xyzw;
        r4.w = dot(r12.wz, r1.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.xy, r1.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r15.xy, r11.zz);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.wz, r11.zz);
        r0.z = mad(r7.y, r4.w, r0.z);
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(3, 5)).xyzw;
        r4.w = dot(r12.wz, r1.ww);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.xy, r1.ww);
        r0.z = mad(r7.y, r4.w, r0.z);
        r4.w = dot(r16.xy, r11.zz);
        r0.z = mad(r9.y, r4.w, r0.z);
        r4.w = dot(r12.wz, r11.zz);
        r0.z = mad(r7.y, r4.w, r0.z);
        r8.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.y, int2(5, 5)).xyzw;
        r7.xz = r8.zy * r7.xx;
        r7.xz = r7.xz * r1.ww;
        r7.zw = mad(r8.wx, r1.ww, r7.xz);
        r0.z = mad(r9.y, r7.z, r0.z);
        r0.z = mad(r7.y, r7.w, r0.z);
        r0.z = mad(r9.y, r5.y, r0.z);
        r1.w = mad(r8.w, r10.z, r7.x);
        r0.z = mad(r7.y, r1.w, r0.z);
        r5.x = r0.z / r9.z;
      }
    } else {
      r5.x = 0;
    }
  } else {
    r5.x = 1;
  }
  if (r0.x == 0) {
    r6.z = r5.w;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r6.xyz).x;
    r0.x = cmp(r0.y < r0.x);
    r5.x = r0.x ? 1.000000 : 0;
  }
  r0.x = dot(r4.xyz, r4.xyz);
  r0.y = rsqrt(r0.x);
  r4.xyz = r4.xyz * r0.yyy;
  r1.xyz = -camera_position.xyz + r1.xyz;
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = rsqrt(r0.y);
  r1.xyz = r1.xyz * r0.yyy;
  r0.y = cmp(0 < g_hdr_on);
  r0.y = r0.y ? 0.00400000019 : 1;
  r0.z = dot(r2.xyz, -r4.xyz);
  r1.w = max(0, r0.z);
  r5.yzw = mad(r0.zzz, float3(0.800000012,0.899999976,0.300000012), float3(0.300000012,0.5,0.300000012));
  r5.yzw = saturate(float3(0.694444418,0.694444418,0.444444448) * r5.yzw);
  r6.xyz = float3(0.800000012,0.699999988,0.200000003) * r5.zzz;
  r4.w = mad(r0.z, r5.y, 0.300000012);
  r4.w = saturate(1 + -r4.w);
  r7.xyz = r6.xyz * r4.www;
  r8.xyz = float3(0.600000024,0.200000003,0.100000001) * r5.www;
  r5.z = mad(-r0.z, r5.y, 1);
  r8.xyz = r8.xyz * r5.zzz;
  r6.xyz = mad(-r6.xyz, r4.www, float3(1,1,1));
  r5.yzw = mad(r0.zzz, r5.yyy, r7.xyz);
  r5.yzw = mad(r8.xyz, r6.xyz, r5.yzw);
  r5.yzw = r5.yzw + -r0.zzz;
  r5.yzw = saturate(mad(r0.www, r5.yzw, r0.zzz));
  r3.xyz = r5.yzw * r3.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r3.xyz = float3(0.899999976,0.899999976,0.899999976) * r3.xyz;
  r0.z = dot(r4.xyz, r2.xyz);
  r0.z = r0.z + r0.z;
  r2.xyz = mad(r2.xyz, -r0.zzz, r4.xyz);
  r0.z = dot(r2.xyz, -r1.xyz);
  r0.z = max(0, r0.z);
  r0.w = mad(r2.w, 63, 1.60000002);
  r0.z = log2(r0.z);
  r0.z = r0.w * r0.z;
  r0.z = exp2(r0.z);
  r0.z = min(1, r0.z);
  r0.z = r3.w * r0.z;
  r0.z = r0.z * r1.w;
  r1.xyz = v2.xyz * r0.zzz;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / v3.w);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r0.x;
  r0.x = r0.x * r5.x;
  r0.yzw = mad(r1.xyz, r0.yyy, r3.xyz);
  o0.xyz = r0.xxx * r0.yzw;
  o0.w = 1;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}