// PsychoV35 alone (twattila/psycho35.hlsli's ApplyPsychoV35, the one copy every game calls) on one case per thread, for
// tests.cpp part 9: run.ps1 compiles it with this tree's Attila folder and with -Before's (the control) as the include
// path. Its sliders: the shader_injection cbuffer (Attila's shared.h).
#include "shared.h"
#include "psycho35.hlsli"

struct EdgeCase {
  float4 color_and_peak;  // xyz linear BT.709 (Paper White 1), w Peak over Paper White
  float4 output;          // x 1: SDR output (BT.709 target), else BT.2020
};

StructuredBuffer<EdgeCase> cases : register(t0);
RWStructuredBuffer<float4> results : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const EdgeCase test = cases[id.x];
  results[id.x] = float4(ApplyPsychoV35(test.color_and_peak.xyz, test.color_and_peak.w, test.output.x != 0.f), 0.f);
}
