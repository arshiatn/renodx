/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// rt_outside_view.gs_5_0.hlsl for a shadow-pass draw the game's pixel shader cuts out (fx_weighted's BADA4FE3, 46E626D5 with B2358145:
// its base colour map's alpha at TEXCOORD1): the corners' UV passed on for the rays' mask (rt_core.hpp SceneShader::cascade_keep_uv).
// CMake's shader rule takes no gs_ target: after a change run in this folder
// bin\fxc.exe rt_outside_view_uv.gs_5_0.hlsl -T gs_5_0 -E main /O3 /Qstrip_reflect /nologo /Vn __rt_outside_view_uv /Fh rt_outside_view_uv_gs.h
struct Corner {
  float4 position : SV_Position;
  float2 uv : TEXCOORD1;
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
