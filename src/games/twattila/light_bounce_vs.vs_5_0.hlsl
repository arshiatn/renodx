/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Light Bounce (Experimental): the trace's full-screen triangle (light_bounce.hpp), from the vertex index alone.
void main(uint id : SV_VertexID, out float4 position : SV_Position) {
  const float2 uv = float2((id << 1) & 2, id & 2);
  position = float4(uv * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
}
