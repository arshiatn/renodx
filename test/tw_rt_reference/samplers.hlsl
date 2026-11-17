/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The reference build's samplers (src/games/twattila/dlaa_helper/rt_reference.hlsli) on WARP for tests.cpp: one sample a
// thread from its input's uniform numbers. ref_word: the sampler; ref_a: the thread count; ref_b: the random numbers' sample
// index (0) or the strata (5).
#include "rt_reference.hlsli"

struct TestInput {
  float4 u;      // xy the sample's uniform numbers, z the GGX width (4)
  float4 frame;  // xyz the normal (2, 3) or the view in the surface's frame, z along the normal (4)
};
StructuredBuffer<TestInput> test_inputs : register(t100);
RWStructuredBuffer<float4> test_outputs : register(u100);

[numthreads(64, 1, 1)]
void main(uint id : SV_DispatchThreadID) {
  if (id >= ref_a) return;
  const TestInput input = test_inputs[id];
  float4 output = 0.f;
  switch (ref_word) {
    case 0u: {
      // Four dimensions of one sample of texel (id % 512, id / 512).
      const uint2 texel = uint2(id % 512u, id / 512u);
      output = float4(RefRandom(texel, ref_b, 0u), RefRandom(texel, ref_b, 1u), RefRandom(texel, ref_b, 2u), RefRandom(texel, ref_b, 3u));
      break;
    }
    case 1u:
      output = float4(RefConcentricDisk(input.u.xy), 0.f, 0.f);
      break;
    case 2u:
      output = float4(RefCosineDirection(input.frame.xyz, input.u.xy), 0.f);
      break;
    case 3u: {
      float3 tangent;
      float3 bitangent;
      RefBasis(input.frame.xyz, tangent, bitangent);
      output = float4(dot(tangent, bitangent), dot(tangent, input.frame.xyz), dot(bitangent, input.frame.xyz), dot(cross(tangent, bitangent), input.frame.xyz));
      break;
    }
    case 4u: {
      // The microfacet normal and the reflected direction's weight G2 / G1 (as the reference's REFLECT slice takes them).
      const float3 view = input.frame.xyz;
      const float3 half_vector = RefVisibleNormal(view, input.u.z, input.u.xy);
      const float3 light = 2.f * dot(view, half_vector) * half_vector - view;
      output = float4(half_vector, RefVisibleNormalWeight(view.z, light.z, input.u.z));
      break;
    }
    default:
      output = float4(RefStratified(id % ref_b, ref_b, input.u.xy), 0.f, 0.f);
      break;
  }
  test_outputs[id] = output;
}
