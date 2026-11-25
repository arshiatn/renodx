/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp's probe: src/games/twattila/callisto_diffuse.hlsli per record, compiled by fxc (cs_5_0, D3D11 WARP) and
// dxc (cs_6_5, D3D12 WARP). `main`: every named set as the compilers fold it and the record's own set (bound sets,
// the talk's defaults, controls). `vmf`: the merged VmfLobe at base 0.5, r 0.16 / 0.40 (T-vmf). `cost` / `cost_base`
// (compiled with CALLISTO_COST_SET defined, never run): one lobe of that set, and the same loads and store with the
// lobe's multiply and two more in its place; the static cost line is their difference. The record count is a multiple
// of 64 (no bounds: a D3D12 root descriptor has no size).
#include "../../src/games/twattila/callisto_diffuse.hlsli"
#include "../../src/games/twattila/vmf_diffuse.hlsli"

struct ProbeInput {
  CallistoSet set;
  float n_dot_l;
  float n_dot_v;
  float l_dot_v;
  float amount;
};

struct ProbeOutput {
  float set_a;
  float set_a_f;
  float set_v16;
  float set_v16_di;
  float set_v40;
  float set_v40_di;
  float set_planted;
  float own_set;
};

StructuredBuffer<ProbeInput> inputs : register(t0);
RWStructuredBuffer<ProbeOutput> outputs : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const ProbeInput input = inputs[id.x];
  ProbeOutput output;
  output.set_a = CallistoLobe(CALLISTO_SET_A, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_a_f = CallistoLobe(CALLISTO_SET_A_F, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_v16 = CallistoLobe(CALLISTO_SET_V16, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_v16_di = CallistoLobe(CALLISTO_SET_V16_DI, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_v40 = CallistoLobe(CALLISTO_SET_V40, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_v40_di = CallistoLobe(CALLISTO_SET_V40_DI, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.set_planted = CallistoLobe(CALLISTO_SET_PLANTED, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  output.own_set = CallistoLobe(input.set, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
  outputs[id.x] = output;
}

StructuredBuffer<float3> vmf_inputs : register(t1);  // n.l, n.v, l.v
RWStructuredBuffer<float2> vmf_outputs : register(u1);

[numthreads(64, 1, 1)]
void vmf(uint3 id : SV_DispatchThreadID) {
  const float3 input = vmf_inputs[id.x];
  vmf_outputs[id.x] = float2(VmfLobe(input.x, input.y, input.z, 0.5f, 0.16f).x,
                             VmfLobe(input.x, input.y, input.z, 0.5f, 0.40f).x);
}

#ifdef CALLISTO_COST_SET
RWStructuredBuffer<float> cost_outputs : register(u2);

[numthreads(64, 1, 1)]
void cost(uint3 id : SV_DispatchThreadID) {
  const ProbeInput input = inputs[id.x];
  cost_outputs[id.x] = input.n_dot_l
                       * CallistoLobe(CALLISTO_COST_SET, input.n_dot_l, input.n_dot_v, input.l_dot_v, input.amount);
}

[numthreads(64, 1, 1)]
void cost_base(uint3 id : SV_DispatchThreadID) {
  const ProbeInput input = inputs[id.x];
  cost_outputs[id.x] = input.n_dot_l * input.n_dot_v * input.l_dot_v * input.amount;
}
#endif
