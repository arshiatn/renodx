// Hue Clip (psycho_test31.hlsli's HueClip) alone, for tests.cpp: run.ps1 compiles it once with Attila's folder and once
// with WH3's as the include path (/I), so both copies are tested.
#include "shared.h"
#include "psycho_test31.hlsli"

struct HueClipCase {
  float4 color_and_strength;  // xyz the colour, w the strength
  float4 clip_scale;          // x
};

StructuredBuffer<HueClipCase> cases : register(t0);
RWStructuredBuffer<float4> results : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const HueClipCase test = cases[id.x];
  results[id.x] = float4(
      renodx::tonemap::psychov::HueClip(test.color_and_strength.xyz, test.clip_scale.x, test.color_and_strength.w), 0.f);
}
