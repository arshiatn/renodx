// 0x259AEC56 (battlefog ps30_main, data_rome2.pack: data.pack's 0x890A842B is overridden): the battle's height and
// distance fog, its colour the sun-lit fog colour blended toward the sky's. Fog & Sky Smoothing (twattila/sky_smooth.hlsli).
#include "../shared.h"
#include "../../twattila/render_scale.hlsli"
#include "../../twattila/sky_smooth.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 inv_view_projection : packoffset(c21);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
}

cbuffer lighting_VS_PS : register(b1)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float g_hdr_on : packoffset(c11);
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

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0, r1;

  r0.xy = (g_vpos_texel_offset + v0.xy) * g_screen_size.zw;
  r1.xyzw = gbuffer_channel_4_sampler.SampleLevel(gbuffer_channel_4_sampler_s, r0.xy, r0.y).yzxw;
  // Below 100% the view fills its share of the target (view_share.hpp patches the game's own).
  r1.xy = (r0.xy / DrawRenderShare()) * float2(2, -2) + float2(-1, 1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r0.w = g_fog_height_top - r0.y;
  r1.xyz = r0.xyz - camera_position.xyz;
  r0.x = saturate((r0.w - g_fog_height_bottom) / (g_fog_height_top - g_fog_height_bottom));
  r0.x = r0.x * r0.x * (3 - 2 * r0.x);
  const float distance_to_camera = length(r1.xyz);
  r0.z = distance_to_camera / ((1 - g_fog_distance_strength) * 1000) + (1 - g_fog_distance_start) * 8 - 4;
  r0.z = g_fog_distance_scale / exp2(1.44269502 * r0.z);
  r0.z = saturate(g_fog_distance_scale - r0.z);
  r0.x = g_fog_height_strength * r0.x + r0.z;
  r0.z = saturate(dot(r0.zz, g_fog_colour_blend));
  r0.y = min(1, distance_to_camera / max(0.00100000005, g_fog_clear_distance));
  r0.y = r0.y * r0.y * (3 - 2 * r0.y);
  o0.w = saturate(r0.x * r0.y);
  r1.w = max(0, r1.y);
  float3 sky = s_sky.Sample(s_sky_s, r1.xwz).xyz;
  // No fog or no sky in it: not needed.
  [branch]
  if (SI.sky_smooth != 0.f && r0.x > 0.f && r0.z > 0.f) {
    sky = SkySmooth(s_sky, s_sky_s, r1.xwz);
  }
  const float3 sun_lit = sun_colour.xyz * g_volume_fog_colour.xyz * 1.5 * abs(sun_direction.y);
  const float sun_lit_scale = (0 < g_hdr_on) ? 0.00400000019 : 1;
  o0.xyz = r0.z * (sky - sun_lit * sun_lit_scale) + sun_lit * sun_lit_scale;
}
