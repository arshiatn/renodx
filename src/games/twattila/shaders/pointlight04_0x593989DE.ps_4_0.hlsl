// 0x593989DE (pointlightlinearfalloff slm highdef): deferred point lights, instanced (the light's position
// and reach per instance: POSITION2, v3). Ray tracing: x the light's traced visibility
// (twattila/rt_scene.hpp InstallPointLights; rt_local_lights.hlsli).
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

SamplerState s_ssao_s : register(s0);
SamplerState gbuffer_channel_1_sampler_s : register(s1);
SamplerState gbuffer_channel_2_sampler_s : register(s2);
SamplerState gbuffer_channel_3_sampler_s : register(s3);
SamplerState gbuffer_channel_4_sampler_s : register(s4);
Texture2D<float4> gbuffer_channel_1_sampler : register(t0);
Texture2D<float4> gbuffer_channel_2_sampler : register(t1);
Texture2D<float4> gbuffer_channel_3_sampler : register(t2);
Texture2D<float4> gbuffer_channel_4_sampler : register(t3);
Texture2D<float4> s_ssao : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  r1.xyzw = gbuffer_channel_1_sampler.SampleLevel(gbuffer_channel_1_sampler_s, r0.zw, r0.w).xyzw;
  r2.xyzw = gbuffer_channel_2_sampler.SampleLevel(gbuffer_channel_2_sampler_s, r0.zw, r0.w).xyzw;
  r3.xyzw = gbuffer_channel_3_sampler.SampleLevel(gbuffer_channel_3_sampler_s, r0.zw, r0.w).xyzw;
  r4.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.zw, r0.w).yzxw;
  r2.xyz = r2.xyz * r2.xyz;
  r1.xyz = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r3.w = dot(r1.xyz, r1.xyz);
  r3.w = rsqrt(r3.w);
  r1.xyz = r3.www * r1.xyz;
  // The AO blur draws its whole target below 100% (view_share.hpp RegionRead::whole).
  r5.xyzw = s_ssao.Sample(s_ssao_s, r0.zw / DrawRenderShare()).xyzw;
  r0.xy = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = r0.xy / g_viewport_offset_scale.zw;
  // Below 100% the view fills its share of the target (view_share.hpp patches the game's own).
  r4.xy = (r0.xy / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r0.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r4.xyz = -v3.xyz + r0.xyz;
  r0.w = dot(r4.xyz, r4.xyz);
  r3.w = rsqrt(r0.w);
  r4.xyz = r4.xyz * r3.www;
  r0.w = sqrt(r0.w);
  r0.w = saturate(r0.w / v3.w);
  r0.w = 1 + -r0.w;
  r3.w = cmp(0 < g_hdr_on);
  r5.yz = r3.ww ? float2(0.00400000019,1) : float2(1,273);
  r4.w = dot(r1.xyz, -r4.xyz);
  r5.w = max(0, r4.w);
  r4.w = cmp(0 < r4.w);
  if (r4.w != 0) {
    r0.xyz = -camera_position.xyz + r0.xyz;
    r4.w = dot(r0.xyz, r0.xyz);
    r4.w = rsqrt(r4.w);
    r0.xyz = r4.www * r0.xyz;
    r4.w = 1 + -r1.w;
    r6.x = dot(r0.xyz, r1.xyz);
    r6.x = r6.x + r6.x;
    r1.xyz = r1.xyz * -r6.xxx + r0.xyz;
    r1.x = dot(-r4.xyz, r1.xyz);
    r1.x = max(-1, r1.x);
    r1.x = min(1, r1.x);
    r1.y = 1 + -abs(r1.x);
    r1.y = sqrt(r1.y);
    r1.z = abs(r1.x) * -0.0187292993 + 0.0742610022;
    r1.z = r1.z * abs(r1.x) + -0.212114394;
    r1.z = r1.z * abs(r1.x) + 1.57072878;
    r6.x = r1.z * r1.y;
    r6.x = r6.x * -2 + 3.14159274;
    r1.x = cmp(r1.x < -r1.x);
    r1.x = r1.x ? r6.x : 0;
    r1.x = r1.z * r1.y + r1.x;
    r1.y = r1.w * r1.w;
    r1.z = r3.w ? 0.995899975 : -0.000100016594;
    r1.y = r1.y * r1.z + r5.y;
    r1.y = r1.y * 0.5 + 0.5;
    r1.y = r1.y * 2 + -1;
    r1.z = -r1.y * r1.y + 1;
    r1.z = max(0.00100000005, r1.z);
    r1.z = log2(r1.z);
    r1.w = 4.95061684 * r1.z;
    r1.z = r1.z * 0.346573591 + 4.54688501;
    r3.w = cmp(0 < r1.y);
    r1.y = cmp(r1.y < 0);
    r1.y = (int)-r3.w + (int)r1.y;
    r1.y = (int)r1.y;
    r1.w = r1.z * r1.z + -r1.w;
    r1.w = sqrt(r1.w);
    r1.z = r1.w + -r1.z;
    r1.z = max(0, r1.z);
    r1.z = sqrt(r1.z);
    r1.y = r1.y * r1.z;
    r1.y = 1.41421354 * r1.y;
    r1.y = 0.00872664619 / r1.y;
    r1.y = max(9.99999975e-005, r1.y);
    r1.xz = float2(-0.00872664619,0.00872664619) + r1.xx;
    r1.y = 1 / r1.y;
    r1.xy = r1.xz * r1.yy;
    r1.zw = float2(0.707106769,0.707106769) * r1.xy;
    r1.zw = r1.zw * r1.zw;
    r6.xyzw = r1.zzww * float4(0.140012279,0.140012279,0.140012279,0.140012279) + float4(1.27323949,1,1.27323949,1);
    r6.xy = r6.xz / r6.yw;
    r1.zw = r6.xy * -r1.zw;
    r6.xy = cmp(float2(0,0) < r1.xy);
    r1.x = cmp(r1.x < 0);
    r1.x = (int)-r6.x + (int)r1.x;
    r1.x = (int)r1.x;
    r1.yz = float2(1.44269502,1.44269502) * r1.zw;
    r1.yz = exp2(r1.yz);
    r1.yz = float2(1,1) + -r1.yz;
    r1.yz = sqrt(r1.yz);
    r1.x = r1.x * r1.y + 1;
    r1.x = 0.5 * r1.x;
    r1.y = (int)-r6.y;
    r1.y = r1.y * r1.z + 1;
    r1.x = r1.y * 0.5 + -r1.x;
    r1.y = min(1, r5.w);
    r1.zw = float2(1.57079637,1.53938043) * r4.ww;
    r1.z = sin(r1.z);
    r1.y = -1 + r1.y;
    r1.y = r1.z * r1.y + 1;
    r0.x = dot(-r4.xyz, r0.xyz);
    r0.x = max(0, r0.x);
    r0.x = log2(r0.x);
    r0.x = 10 * r0.x;
    r0.x = exp2(r0.x);
    r0.y = cos(r1.w);
    r0.y = rsqrt(r0.y);
    r0.y = 1 / r0.y;
    r0.z = saturate(60 * r2.w);
    r0.x = r0.x * r0.y;
    r0.y = r0.z + -r2.w;
    r0.x = r0.x * r0.y + r2.w;
    r0.y = abs(r1.x) * r1.y;
    r0.xyz = r0.yyy * r0.xxx;
  } else {
    r0.xyz = float3(0,0,0);
  }
  r1.xyz = saturate(r0.xyz * r5.zzz);
  r1.xyz = r1.xyz * r3.xyz;
  r0.xyz = max(r0.xyz, r2.www);
  r0.xyz = float3(1,1,1) + -r0.xyz;
  r2.xyz = r5.www * r2.xyz;
  r2.xyz = v2.xyz * r2.xyz;
  r0.xyz = r2.xyz * r0.xyz;
  r0.xyz = r0.xyz * r5.yyy;
  r0.xyz = r1.xyz * v2.xyz + r0.xyz;
  r0.xyz = r0.www * r0.xyz;
  r0.w = 1;
  o0.xyzw = r5.xxxx * r0.xyzw;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  return;
}
