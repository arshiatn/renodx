// The add-on's grade after the curve (shaders/tonemappers.hlsli GradeAfterCurve) alone, for tests.cpp: run.ps1 compiles
// it with the add-on's shader folder as the include path.
#include "tonemappers.hlsli"

struct GradeCase {
  float4 color;
  row_major float4x4 color_matrix;
  float4 source_color;
  float4 target_color;
};

StructuredBuffer<GradeCase> cases : register(t0);
RWStructuredBuffer<float4> results : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const GradeCase test = cases[id.x];
  results[id.x] = divinity_tonemap::GradeAfterCurve(test.color.rgb, test.color_matrix, test.source_color.rgb,
                                                    test.target_color.rgb);
}
