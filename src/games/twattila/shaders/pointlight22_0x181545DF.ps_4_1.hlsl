// 0x181545DF (pointlightsmoothfalloffshadow slm highdef, gather 4.1: the game's point light shadows on, Cutscene Point
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
SamplerState s_point_light_shadow_map_s : register(s5);
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
  precise float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
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
  r6.z = (int)r6.x;
  r0.z = cmp((int)r0.x == -1);
  if (r0.z != 0) {
    r8.xyzw = mad(g_shadow_buffer_size.yxxx, r7.yxxx, float4(0.5,0.5,0.5,0.5));
    r9.xy = floor(r8.wx);
    r8.xyzw = -r9.yxxx + r8.xyzw;
    r6.xy = g_shadow_buffer_size.zw * r9.xy;
    r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xyz, int2(-1, -1)).xyzw;
    r9.xyzw = cmp(r0.yyyy < r9.wzxy);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r10.xyzw = float4(1,1,2,3) + -r8.xyzw;
    r11.xyzw = r10.yzyz * r9.xyzw;
    r8.yz = r11.xz + r11.yw;
    r11.x = r10.x * r8.y;
    r11.y = r8.x * r8.z;
    r12.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xyz, int2(1, -1)).xyzw;
    r12.xyzw = cmp(r0.yyyy < r12.xyzw);
    r12.xyzw = r12.xyzw ? float4(1,1,1,1) : 0;
    r13.xyz = float3(1,1,2) + r8.www;
    r0.z = r13.z * r12.x;
    r8.yz = r12.zy * r8.ww;
    r9.xy = mad(r12.wx, r13.yy, r8.yz);
    r11.z = r10.x * r9.x;
    r11.w = r9.y * r8.x;
    r1.w = dot(r11.xyzw, float4(1,1,1,1));
    r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xyz, int2(-1, 1)).xyzw;
    r11.xyzw = cmp(r0.yyyy < r11.wzxy);
    r11.xyzw = r11.xyzw ? float4(1,1,1,1) : 0;
    r12.xyzw = r11.xyzw * r10.yzyz;
    r9.xy = r12.xz + r12.yw;
    r12.x = r10.x * r9.x;
    r12.y = r9.y * r8.x;
    r4.w = mad(-r8.w, 2, 2);
    r5.w = r10.w * r9.w;
    r5.w = mad(r9.z, r4.w, r5.w);
    r12.z = r10.x * r5.w;
    r5.w = r11.y * r10.w;
    r4.w = mad(r11.x, r4.w, r5.w);
    r12.w = r8.x * r4.w;
    r4.w = dot(r12.xyzw, float4(1,1,1,1));
    r1.w = r4.w + r1.w;
    r9.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r6.xyz, int2(1, 1)).xyzw;
    r9.xyzw = cmp(r0.yyyy < r9.xyzw);
    r9.xyzw = r9.xyzw ? float4(1,1,1,1) : 0;
    r4.w = r9.w * r13.z;
    r6.xy = r9.zy * r8.ww;
    r6.yw = mad(r9.wx, r13.xy, r6.xy);
    r9.x = r10.x * r6.y;
    r9.y = r8.x * r6.w;
    r0.z = mad(r8.z, 2, r0.z);
    r9.z = r10.x * r0.z;
    r0.z = mad(r6.x, 2, r4.w);
    r9.w = r8.x * r0.z;
    r0.z = dot(r9.xyzw, float4(1,1,1,1));
    r0.z = r1.w + r0.z;
    r0.z = 0.0666666701 * r0.z;
  } else {
    r0.z = 1;
  }
  if (r0.x == 0) {
    r7.z = r6.z;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r7.xyz).x;
    r0.x = cmp(r0.y < r0.x);
    r0.z = r0.x ? 1.000000 : 0;
  }
  r0.x = dot(r5.xyz, r5.xyz);
  r0.y = rsqrt(r0.x);
  r5.xyz = r5.xyz * r0.yyy;
  r0.x = sqrt(r0.x);
  r0.x = saturate(r0.x / v3.w);
  r0.x = 1 + -r0.x;
  r0.x = r0.x * r0.x;
  r0.y = cmp(0 < g_hdr_on);
  r6.xy = r0.yy ? float2(0.00400000019,1) : float2(1,273);
  r1.w = dot(r2.xyz, -r5.xyz);
  r4.w = max(0, r1.w);
  r1.w = cmp(0 < r1.w);
  if (r1.w != 0) {
    r1.xyz = -camera_position.xyz + r1.xyz;
    r1.w = dot(r1.xyz, r1.xyz);
    r1.w = rsqrt(r1.w);
    r1.xyz = r1.xyz * r1.www;
    r1.w = 1 + -r2.w;
    r5.w = dot(r1.xyz, r2.xyz);
    r5.w = r5.w + r5.w;
    r2.xyz = mad(r2.xyz, -r5.www, r1.xyz);
    r2.x = dot(-r5.xyz, r2.xyz);
    r2.x = max(-1, r2.x);
    r2.x = min(1, r2.x);
    r2.y = 1 + -abs(r2.x);
    r2.y = sqrt(r2.y);
    r2.z = mad(abs(r2.x), -0.0187292993, 0.0742610022);
    r2.z = mad(r2.z, abs(r2.x), -0.212114394);
    r2.z = mad(r2.z, abs(r2.x), 1.57072878);
    r5.w = r2.z * r2.y;
    r5.w = mad(r5.w, -2, 3.14159274);
    r2.x = cmp(r2.x < -r2.x);
    r2.x = r2.x ? r5.w : 0;
    r2.x = mad(r2.z, r2.y, r2.x);
    r2.y = r2.w * r2.w;
    r0.y = r0.y ? 0.995899975 : -0.000100016594;
    r0.y = mad(r2.y, r0.y, r6.x);
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
    r6.zw = r7.xz / r7.yw;
    r2.zw = r6.zw * -r2.zw;
    r6.zw = cmp(float2(0,0) < r2.xy);
    r0.y = cmp(r2.x < 0);
    r0.y = (int)-r6.z + (int)r0.y;
    r0.y = (int)r0.y;
    r2.xy = float2(1.44269502,1.44269502) * r2.zw;
    r2.xy = exp2(r2.xy);
    r2.xy = float2(1,1) + -r2.xy;
    r2.xy = sqrt(r2.xy);
    r0.y = mad(r0.y, r2.x, 1);
    r0.y = 0.5 * r0.y;
    r2.x = (int)-r6.w;
    r2.x = mad(r2.x, r2.y, 1);
    r0.y = mad(r2.x, 0.5, -r0.y);
    r2.x = min(1, r4.w);
    r2.yz = float2(1.57079637,1.53938043) * r1.ww;
    r1.w = sin(r2.y);
    r2.x = -1 + r2.x;
    r1.w = mad(r1.w, r2.x, 1);
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
    r0.y = r1.w * abs(r0.y);
    r1.xyz = r0.yyy * r1.xxx;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r2.xyz = saturate(r1.xyz * r6.yyy);
  r2.xyz = r2.xyz * r4.xyz;
  r1.xyz = max(r1.xyz, r3.www);
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r3.xyz = r4.www * r3.xyz;
  r3.xyz = v2.xyz * r3.xyz;
  r1.xyz = r3.xyz * r1.xyz;
  r1.xyz = r1.xyz * r6.xxx;
  r1.xyz = mad(r2.xyz, v2.xyz, r1.xyz);
  r1.xyz = r1.xyz * r0.zzz;
  r1.xyz = r1.xyz * r0.xxx;
  r1.w = 1;
  o0.xyzw = r1.xyzw * r0.wwww;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}