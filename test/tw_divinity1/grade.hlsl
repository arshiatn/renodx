// The add-on's grade after the curve (shaders/tonemappers.hlsli GradeAfterCurve) alone, for tests.cpp: run.ps1 compiles
// it with the add-on's shader folder as the include path, entry `main`. Entry `control` is the grade it replaced (a
// hard knee at 1: graded at the brightest channel's 1 and scaled back), the failing control of check 4's slope test.
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

[numthreads(64, 1, 1)]
void control(uint3 id : SV_DispatchThreadID) {
  const GradeCase test = cases[id.x];
  const float scale = max(renodx::math::Max(test.color.rgb), 1.f);
  const float3 normalized = test.color.rgb / scale;
  const float3 inside = saturate(normalized);
  const float4 encoded = float4(pow(inside, 1.f / 2.2f), 1.f);
  float4 graded = float4(dot(test.color_matrix._m00_m01_m02_m03, encoded), dot(test.color_matrix._m10_m11_m12_m13, encoded),
                         dot(test.color_matrix._m20_m21_m22_m23, encoded), dot(test.color_matrix._m30_m31_m32_m33, encoded));
  const float3 toward_target = clamp(test.target_color.rgb + graded.rgb, -1.f, 1.f) - graded.rgb;
  const float weight = max(0.f, 1.f - length(graded.rgb - test.source_color.rgb));
  graded.rgb = max(toward_target * (weight * weight) + graded.rgb, 0.f);
  results[id.x] = float4((renodx::color::gamma::Decode(graded.rgb, 2.2f) + normalized - inside) * scale, graded.a);
}
