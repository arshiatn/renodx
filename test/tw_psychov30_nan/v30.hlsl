// PsychoV30 (twattila/psycho_test30.hlsli, shared by Attila's family of add-ons and both Divinity games) on one case per
// thread, for tests.cpp. Compiled twice by run.ps1 with the build's flags: as shipped, and with /D NO_GUARDS: every
// isnan / isinf (the file's NaN / infinity guards, renodx's ZeroNaN at its input) false, the failing control.
#ifdef NO_GUARDS
#define isnan(x) false
#define isinf(x) false
#endif
#include "../../src/shaders/renodx.hlsl"
#include "../../src/games/twattila/psycho_test30.hlsli"

struct Case {
  float4 color;   // rgb, peak
  float4 grade;   // exposure, highlights, shadows, contrast
  float4 extra;   // purity, cone response exponent, gamut mode (0 BT.709, 1 BT.2020), anchor
};

StructuredBuffer<Case> cases : register(t0);
RWStructuredBuffer<float4> results : register(u0);

// The guarded steps themselves, on a value computed here (rgb / grade.xyz: 0 / 0 is NaN, x / 0 infinity, as a NaN
// would arise inside the curve). Out: the steps' valid flags (MBFromRelativeLMS, YfCeilingSolve), whether
// AnchorSourcePositiveTotalToYf kept the total (0 = rejected), and the YfCeilingSolve result's finiteness.
[numthreads(64, 1, 1)]
void guards(uint3 id : SV_DispatchThreadID) {
  const Case test = cases[id.x];
  const float3 value = test.color.rgb / test.grade.xyz;
  uint mb_valid = 0u;
  renodx_custom::tonemap::psycho30::psycho30_MBFromRelativeLMS(value, mb_valid);
  uint ceiling_valid = 0u;
  const float3 ceiling = renodx_custom::tonemap::psycho30::psycho30_YfCeilingSolve(value, value.y, 1, ceiling_valid);
  const float3 total = renodx_custom::tonemap::psycho30::psycho30_AnchorSourcePositiveTotalToYf(value);
  results[id.x] = float4(mb_valid, ceiling_valid, any(total != 0.f) ? 1.f : 0.f,
                         all((asuint(ceiling) & 0x7fffffffu) < 0x7f800000u) ? 1.f : 0.f);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const Case test = cases[id.x];
  const float3 anchor = test.extra.w;
  results[id.x] = float4(renodx_custom::tonemap::psycho30::psychotm_test30(
                             test.color.rgb, test.color.w, test.grade.x, test.grade.y, test.grade.z, test.grade.w,
                             test.extra.x, 1.f, 100.f, 1.f, 1.f, 0, test.extra.y, anchor, anchor, 1.f,
                             (int)test.extra.z),
                         0.f);
}
