/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's rt_outside_view_uv.gs_5_0.hlsl for Ryse's alpha-tested sun casters (ps 0x9112241F: the diffuse map's
// alpha at TEXCOORD0; vs 48CE820A, B089969C, A870279C, 646698C7, DF6E8CD1, CFE96F1E write SV_Position, TEXCOORD0.xy): the
// game's shadow vertex shader runs with the main view's matrix in PER_FRAME (rt_core Host::cascade_view_slot / _row), a
// triangle in the view collapses onto its first corner (the main pass captured it), the corners' UV go on for the rays'
// mask (rt_core.hpp SceneShader::cascade_keep_uv). The solid casters take Warhammer III's rt_outside_view (SV_Position).
// CMake's shader rule takes no gs_ target: after a change run in this folder
// ..\..\..\bin\fxc.exe rt_outside_view_uv.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rys_outside_view_uv /Fh rt_outside_view_uv_gs.h
struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[maxvertexcount(3)]
void main(triangle Corner corners[3], inout TriangleStream<Corner> stream) {
  const float3 x = float3(corners[0].position.x, corners[1].position.x, corners[2].position.x);
  const float3 y = float3(corners[0].position.y, corners[1].position.y, corners[2].position.y);
  const float3 z = float3(corners[0].position.z, corners[1].position.z, corners[2].position.z);
  const float3 w = float3(corners[0].position.w, corners[1].position.w, corners[2].position.w);
  const bool outside = all(w <= 0.f) || all(x > w) || all(x < -w) || all(y > w) || all(y < -w) || all(z > w);
  for (uint corner = 0u; corner < 3u; ++corner) {
    Corner output;
    output.position = outside ? corners[corner].position : corners[0].position;
    output.uv = corners[corner].uv;
    stream.Append(output);
  }
}
