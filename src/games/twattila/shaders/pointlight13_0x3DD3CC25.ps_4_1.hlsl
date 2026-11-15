// 0x3DD3CC25 (pointlightlinearfalloffshadow skinlm lowdef, gather 4.1: the game's point light shadows on, Cutscene Point
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
SamplerState s_point_light_shadow_map_s : register(s4);
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
  precise float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12;
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
  r5.z = (int)r5.x;
  r0.z = cmp((int)r0.x == -1);
  if (r0.z != 0) {
    r7.xyzw = mad(g_shadow_buffer_size.yxxx, r6.yxxx, float4(0.5,0.5,0.5,0.5));
    r8.xy = floor(r7.wx);
    r7.xyzw = -r8.yxxx + r7.xyzw;
    r5.xy = g_shadow_buffer_size.zw * r8.xy;
    r8.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xyz, int2(-1, -1)).xyzw;
    r8.xyzw = cmp(r0.yyyy < r8.wzxy);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r9.xyzw = float4(1,1,2,3) + -r7.xyzw;
    r10.xyzw = r9.yzyz * r8.xyzw;
    r7.yz = r10.xz + r10.yw;
    r10.x = r9.x * r7.y;
    r10.y = r7.x * r7.z;
    r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xyz, int2(1, -1)).xyzw;
    r11.xyzw = cmp(r0.yyyy < r11.xyzw);
    r11.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
    r12.xyz = float3(1,1,2) + r7.www;
    r0.z = r12.z * r11.x;
    r7.yz = r11.zy * r7.ww;
    r8.xy = mad(r11.wx, r12.yy, r7.yz);
    r10.z = r9.x * r8.x;
    r10.w = r8.y * r7.x;
    r1.w = dot(r10.xyzw, float4(1,1,1,1));
    r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xyz, int2(-1, 1)).xyzw;
    r10.xyzw = cmp(r0.yyyy < r10.wzxy);
    r10.xyzw = r10.xyzw ? float4(1,1,1,1) : 0;
    r11.xyzw = r10.xyzw * r9.yzyz;
    r8.xy = r11.xz + r11.yw;
    r11.x = r9.x * r8.x;
    r11.y = r8.y * r7.x;
    r4.w = mad(-r7.w, 2, 2);
    r5.w = r9.w * r8.w;
    r5.w = mad(r8.z, r4.w, r5.w);
    r11.z = r9.x * r5.w;
    r5.w = r10.y * r9.w;
    r4.w = mad(r10.x, r4.w, r5.w);
    r11.w = r7.x * r4.w;
    r4.w = dot(r11.xyzw, float4(1,1,1,1));
    r1.w = r4.w + r1.w;
    r8.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xyz, int2(1, 1)).xyzw;
    r8.xyzw = cmp(r0.yyyy < r8.xyzw);
    r8.xyzw = r8.xyzw ? float4(1,1,1,1) : 0;
    r4.w = r8.w * r12.z;
    r5.xy = r8.zy * r7.ww;
    r5.yw = mad(r8.wx, r12.xy, r5.xy);
    r8.x = r9.x * r5.y;
    r8.y = r7.x * r5.w;
    r0.z = mad(r7.z, 2, r0.z);
    r8.z = r9.x * r0.z;
    r0.z = mad(r5.x, 2, r4.w);
    r8.w = r7.x * r0.z;
    r0.z = dot(r8.xyzw, float4(1,1,1,1));
    r0.z = r1.w + r0.z;
    r0.z = 0.0666666701 * r0.z;
  } else {
    r0.z = 1;
  }
  if (r0.x == 0) {
    r6.z = r5.z;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r6.xyz).x;
    r0.x = cmp(r0.y < r0.x);
    r0.z = r0.x ? 1.000000 : 0;
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
  r1.w = dot(r2.xyz, -r4.xyz);
  r4.w = max(0, r1.w);
  r5.xyz = mad(r1.www, float3(0.800000012,0.899999976,0.300000012), float3(0.300000012,0.5,0.300000012));
  r5.xyz = saturate(float3(0.694444418,0.694444418,0.444444448) * r5.xyz);
  r6.xyz = float3(0.800000012,0.699999988,0.200000003) * r5.yyy;
  r5.y = mad(r1.w, r5.x, 0.300000012);
  r5.y = saturate(1 + -r5.y);
  r7.xyz = r6.xyz * r5.yyy;
  r8.xyz = float3(0.600000024,0.200000003,0.100000001) * r5.zzz;
  r5.z = mad(-r1.w, r5.x, 1);
  r8.xyz = r8.xyz * r5.zzz;
  r5.yzw = mad(-r6.xyz, r5.yyy, float3(1,1,1));
  r6.xyz = mad(r1.www, r5.xxx, r7.xyz);
  r5.xyz = mad(r8.xyz, r5.yzw, r6.xyz);
  r5.xyz = r5.xyz + -r1.www;
  r5.xyz = saturate(mad(r0.www, r5.xyz, r1.www));
  r3.xyz = r5.xyz * r3.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r3.xyz = r3.xyz * r0.yyy;
  r3.xyz = float3(0.899999976,0.899999976,0.899999976) * r3.xyz;
  r0.w = dot(r4.xyz, r2.xyz);
  r0.w = r0.w + r0.w;
  r2.xyz = mad(r2.xyz, -r0.www, r4.xyz);
  r0.w = dot(r2.xyz, -r1.xyz);
  r0.w = max(0, r0.w);
  r1.x = mad(r2.w, 63, 1.60000002);
  r0.w = log2(r0.w);
  r0.w = r1.x * r0.w;
  r0.w = exp2(r0.w);
  r0.w = min(1, r0.w);
  r0.w = r3.w * r0.w;
  r0.w = r0.w * r4.w;
  r1.xyz = v2.xyz * r0.www;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / v3.w);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r0.z;
  r0.yzw = mad(r1.xyz, r0.yyy, r3.xyz);
  o0.xyz = saturate(r0.xxx * r0.yzw);
  o0.w = 1;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}