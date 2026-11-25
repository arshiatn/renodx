/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp's pixel probe: src/games/twattila/tw_point_light.hlsli drawn as the game's point-light shaders are (fxc, at
// each variant's own target, D3D11 WARP), on their inputs: the pixel's G-buffer texels and SSAO sampled and its world
// position rebuilt as their DXBC does (these lines are the probe's, not the include's), the light from POSITION2 and its
// colour from POSITION1. Target 0: the light and the alpha the game writes; 1-3: the position and SSAO, the light and
// the colour as interpolated (the dxc probe's records).
#include "../../src/games/twattila/tw_point_light.hlsli"

cbuffer camera_VS_PS : register(b0) {
  float3 camera_position : packoffset(c0);
  float4x4 inv_view_projection : packoffset(c21);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
}

cbuffer point_light_PS : register(b2) {
  float4 g_viewport_offset_scale : packoffset(c0);
}

cbuffer probe : register(b4) {
  float probe_diffuse_scale;  // the sun texture's texel 2 w
  uint probe_linear;
  uint probe_low_def;
  uint probe_model;           // TW_POINT_LIGHT_*; 0: probe_models' per pixel
  uint probe_squared_albedo;  // a control: channel 2's rgb squared
}

SamplerState s_ssao : register(s0);
SamplerState gbuffer_channel_1_sampler : register(s1);
SamplerState gbuffer_channel_2_sampler : register(s2);
SamplerState gbuffer_channel_3_sampler : register(s3);
SamplerState gbuffer_channel_4_sampler : register(s4);
Texture2D<float4> gbuffer_channel_1 : register(t0);
Texture2D<float4> gbuffer_channel_2 : register(t1);
Texture2D<float4> gbuffer_channel_3 : register(t2);
Texture2D<float4> gbuffer_channel_4 : register(t3);
Texture2D<float4> ssao_texture : register(t4);
Texture2D<uint> probe_models : register(t6);

void main(float4 v0 : SV_Position0, float4 v1 : TEXCOORD0, float4 v2 : POSITION1, float4 v3 : POSITION2,
          out float4 o0 : SV_Target0, out float4 o1 : SV_Target1, out float4 o2 : SV_Target2,
          out float4 o3 : SV_Target3) {
  const float2 pixel = g_vpos_texel_offset + v0.xy;
  const float2 uv = g_screen_size.zw * pixel;
  TwPointLightSurface surface;
  surface.channel_1 = gbuffer_channel_1.SampleLevel(gbuffer_channel_1_sampler, uv, uv.y);
  surface.channel_2 = gbuffer_channel_2.SampleLevel(gbuffer_channel_2_sampler, uv, uv.y);
  surface.channel_3 = gbuffer_channel_3.SampleLevel(gbuffer_channel_3_sampler, uv, uv.y);
  const float depth = gbuffer_channel_4.SampleLevel(gbuffer_channel_4_sampler, uv, uv.y).x;
  const float ssao = ssao_texture.Sample(s_ssao, uv).x;
  const float2 view = (pixel * g_screen_size.zw - g_viewport_offset_scale.xy) / g_viewport_offset_scale.zw;
  const float4 world = mul(float4(view * float2(2.f, -2.f) + float2(-1.f, 1.f), depth, 1.f), inv_view_projection);
  surface.position = world.xyz / world.w;
  surface.model = (probe_model != 0u) ? probe_model : probe_models.Load(int3(v0.xy, 0));
  if (probe_squared_albedo != 0u) {
    surface.channel_2.rgb = surface.channel_2.rgb * surface.channel_2.rgb;
  }
  const float3 light = TwPointLight(surface, v3, v2.xyz, probe_linear != 0u, camera_position, probe_diffuse_scale,
                                    ssao, probe_low_def != 0u);
  float alpha = 0.f;
  if (surface.model == TW_POINT_LIGHT_SLM) {
    alpha = (probe_low_def != 0u) ? saturate(ssao) : ssao;
  } else if (surface.model == TW_POINT_LIGHT_SKIN) {
    alpha = 1.f;
  }
  o0 = float4(light, alpha);
  o1 = float4(surface.position, ssao);
  o2 = v3;
  o3 = v2;
}
