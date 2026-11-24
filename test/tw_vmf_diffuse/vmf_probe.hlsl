/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp's probe: src/games/twattila/vmf_diffuse.hlsli per record, compiled by fxc (cs_5_0, D3D11 WARP) and dxc
// (cs_6_5, D3D12 WARP). The record count is a multiple of 64 (no bounds: a D3D12 root descriptor has no size).
#include "../../src/games/twattila/vmf_diffuse.hlsli"

struct ProbeInput {
  float n_dot_l;
  float n_dot_v;
  float l_dot_v;
  float r;
  float3 base;
  float rho;
  float slider;
  float erf_x;
};

struct ProbeOutput {
  float3 lobe;
  float fast;
  float roughness;
  float3 lobe_at_roughness;
  float erf;
};

StructuredBuffer<ProbeInput> inputs : register(t0);
RWStructuredBuffer<ProbeOutput> outputs : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const ProbeInput input = inputs[id.x];
  ProbeOutput output;
  output.lobe = VmfLobe(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.base, input.r);
  output.fast = VmfLobeFast(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.r);
  output.roughness = VmfRoughness(input.rho, input.slider);
  output.lobe_at_roughness = VmfLobe(input.n_dot_l, input.n_dot_v, input.l_dot_v, input.base, output.roughness);
  output.erf = VmfErf(input.erf_x).x;
  outputs[id.x] = output;
}
