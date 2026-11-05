/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Warhammer III's ray tracing scene: units the main pass culled but the shadow maps still draw (rt_core.hpp, SceneShader::cascade_keep).
// The game's shadow vertex shader runs with the main view's matrix at cb0[10-13], so its SV_Position is the main view's clip space as
// every other streamed draw's. A triangle in the view belongs to the main pass, which captured it: it collapses onto its first corner
// (the count stays, a stream output's place in the shared buffer is known before the draw). Only a triangle wholly outside stays.
// CMake's shader rule takes no gs_ target: after a change run in this folder
// bin\fxc.exe rt_outside_view.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_outside_view /Fh rt_outside_view_gs.h
struct Corner {
  float4 position : SV_Position;
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
    stream.Append(output);
  }
}
