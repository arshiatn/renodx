/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Private full-screen triangle (3 vertices, no inputs): texture coordinates 0-1 over the whole target. Outputs like
// the game's fullscreen vertex shaders; the passes it draws (the post pass at the display size, the debug capture)
// read only TEXCOORD0.xy.
void main(
    uint id : SV_VertexID,
    out float4 position : SV_Position0,
    out float4 uv : TEXCOORD0,
    out float4 unused : TEXCOORD1) {
  const float2 corner = float2((id << 1) & 2, id & 2);
  position = float4(corner * float2(2.f, -2.f) + float2(-1.f, 1.f), 0.f, 1.f);
  uv = float4(corner, 0.f, 0.f);
  unused = 0.f;
}
