/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The fires' lights for the rays (twattila/rt_scene.hpp LightInstances::stream_shader): the particle lights' draw (PS 643FC156,
// one light volume an instance) again as points, one vertex an instance, through the game's particle vertex shader F5CE48CC;
// each streamed out as the light the pixel shader draws: its world position (TEXCOORD4.xyz, the particle's: the mesh branch
// writes it apart from the volume's vertex), reach (TEXCOORD2.x) and colour (COLOR0.rgb x COLOR0.a x 2500, the pixel
// shader's own). An instance the vertex shader leaves undrawn (emitter left out or not a mesh: SV_Position (-10, -10, -10, 0)
// or 0) gets reach 0. The input is F5CE48CC's whole output signature (WARP tools\tw\troy\fires: the same elements). Pharaoh's
// fires (5AD46115 with 109B79CE, the same output signature: WARP tools\tw\pharaoh\fires) take it too.
// CMake's shader rule takes no gs_ target: after a change run from the repository's root
// bin\fxc.exe src\games\twtroy\rt_particle_lights.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_particle_lights /Fh src\games\twtroy\rt_particle_lights_gs.h

struct ParticleVertex {
  float4 position : SV_POSITION;
  float4 colour : COLOR0;
  uint4 flags : TEXCOORD0;
  float4 uv : TEXCOORD1;
  float4 size : TEXCOORD2;  // x the light's reach (metres)
  float3 texcoord3 : TEXCOORD3;
  float4 centre : TEXCOORD4;  // xyz the light's world position
  float4 texcoord5 : TEXCOORD5;
  float4 texcoord6 : TEXCOORD6;
  float4 texcoord7 : TEXCOORD7;
  float3 texcoord8 : TEXCOORD8;
};

// rt_scene's light record: LocalLight's light (position, reach) then its colour.
struct LightRecord {
  float4 light : POSITION0;
  float4 colour : COLOR0;
};

[maxvertexcount(1)]
void main(point ParticleVertex input[1], inout PointStream<LightRecord> records) {
  LightRecord record;
  record.light = float4(input[0].centre.xyz, (input[0].position.w != 0.f) ? input[0].size.x : 0.f);
  record.colour = float4(input[0].colour.xyz * input[0].colour.www * 2500.f, 0.f);
  records.Append(record);
}
