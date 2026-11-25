/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp's compute probe: src/games/twattila/tw_point_light.hlsli per record, as the helper would run it (dxc cs_6_5
// -O3, D3D12 WARP; fxc cs_5_0 and dxc cs_6_9 compiled only). A record holds what the pixel probe read and rebuilt at a
// pixel (its texels, position, SSAO, interpolated light and colour); the record count is a multiple of 64.
#include "../../src/games/twattila/tw_point_light.hlsli"

struct ProbeRecord {
  float4 channel_1;
  float4 channel_2;
  float4 channel_3;
  float4 position_ssao;
  float4 light;
  float4 colour_scale;  // w: the sun texture's texel 2 w
  float4 camera_position;
  uint model;
  uint linear_falloff;
  uint low_def;
  uint padding;
};

StructuredBuffer<ProbeRecord> records : register(t0);
RWStructuredBuffer<float4> lights : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const ProbeRecord record = records[id.x];
  TwPointLightSurface surface;
  surface.channel_1 = record.channel_1;
  surface.channel_2 = record.channel_2;
  surface.channel_3 = record.channel_3;
  surface.position = record.position_ssao.xyz;
  surface.model = record.model;
  lights[id.x] = float4(TwPointLight(surface, record.light, record.colour_scale.rgb, record.linear_falloff != 0u,
                                     record.camera_position.xyz, record.colour_scale.w, record.position_ssao.w,
                                     record.low_def != 0u),
                        0.f);
}
