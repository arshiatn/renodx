// 0xA6A0F887 (battlefog, shader model 3): the battle's height and distance fog, its colour the sun-lit fog colour
// blended toward the sky's. Ray tracing: the sun-lit colour x the sun's visibility along the camera's ray, plus the
// torches' and fires' light along it (twattila/rt_scene.hpp InstallFogShadows; rt_fog.hlsli).
#include "../render_scale.hlsli"
#include "../rt_fog.hlsli"
#include "../sky_smooth.hlsli"

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

cbuffer fog_VS_PS : register(b2)
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

SamplerState s_sky_s : register(s0);
SamplerState gbuffer_channel_4_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_sampler : register(t0);
TextureCube<float4> s_sky : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  r1.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.xy, r0.y).yzxw;
  // Below 100% the view fills its share of the target (view_share.hpp patches the game's own).
  r1.xy = (r0.xy / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r0.w = g_fog_height_top + -r0.y;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r0.x = -g_fog_height_bottom + r0.w;
  r0.y = g_fog_height_top + -g_fog_height_bottom;
  r0.y = 1 / r0.y;
  r0.x = saturate(r0.x * r0.y);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.y = dot(r1.xyz, r1.xyz);
  r0.y = sqrt(r0.y);
  r0.z = saturate(g_fog_distance_strength);
  r0.z = 1 + -r0.z;
  r0.z = 1000 * r0.z;
  r0.y = r0.y / r0.z;
  r0.z = saturate(g_fog_distance_start);
  r0.z = 1 + -r0.z;
  r0.z = r0.z * 8 + -4;
  r0.y = r0.z + r0.y;
  r0.y = 1.44269502 * r0.y;
  r0.y = exp2(r0.y);
  r0.y = g_fog_distance_scale / r0.y;
  r0.y = saturate(g_fog_distance_scale + -r0.y);
  r0.x = g_fog_height_strength * r0.x + r0.y;
  r0.y = saturate(dot(r0.xx, g_fog_colour_blend));
  r1.w = max(0, r1.y);
  r2.xyzw = s_sky.Sample(s_sky_s, r1.xwz).xyzw;
  // No fog or no sky in it: not needed.
  [branch]
  if (SI.sky_smooth != 0.f && r0.x > 0.f && r0.y > 0.f) {
    r2.xyz = SkySmooth(s_sky, s_sky_s, r1.xwz);
  }
  r0.z = dot(r1.xz, r1.xz);
  r0.z = sqrt(r0.z);
  r1.xyz = g_volume_fog_colour.xyz * sun_colour.xyz * RtFogSun(v0.xy);
  r1.xyz = float3(1.5,1.5,1.5) * r1.xyz;
  // The game's |cb1[0].z|; the decompiler wrote g_apply_environment_specular (c0.x, unused here): 0 on the campaign, so its
  // fog lost the sun-lit colour and came out black instead of a haze (ATN 2026-11-01).
  r1.xyz = abs(sun_direction.y) * r1.xyz;
  r0.w = cmp(0 < g_hdr_on);
  r0.w = r0.w ? 0.00400000019 : 1;
  r2.xyz = -r1.xyz * r0.www + r2.xyz;
  r1.xyz = r1.xyz * r0.www;
  o0.xyz = r0.yyy * r2.xyz + r1.xyz;
  o0.xyz += g_volume_fog_colour.xyz * RtFogLights(v0.xy) * (1.5 * r0.w);
  r0.y = max(0.00100000005, g_fog_clear_distance);
  r0.y = 1 / r0.y;
  r0.y = r0.z * r0.y;
  r0.y = min(1, r0.y);
  r0.z = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r0.z * r0.y;
  o0.w = saturate(r0.y * r0.x * SI.fog_strength);
  return;
}
