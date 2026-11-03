/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Smoke shadows without rays (smoke_map.hpp): a splat's quad (instanced, no vertex buffer).
#include "./smoke_map.hlsli"

StructuredBuffer<SmokeSplat> splats : register(t0);

static const float2 CORNERS[6] = {float2(-1.f, -1.f), float2(-1.f, 1.f), float2(1.f, 1.f),
                                  float2(-1.f, -1.f), float2(1.f, 1.f), float2(1.f, -1.f)};

void main(uint vertex : SV_VertexID, uint instance : SV_InstanceID, out float4 position : SV_Position, out float2 disk : TEXCOORD0,
          nointerpolation out float4 optical : TEXCOORD1)
{
  const SmokeSplat splat = splats[instance];
  disk = CORNERS[vertex];
  optical = splat.optical;
  position = float4(splat.centre + disk * splat.radius, 0.f, 1.f);
}
