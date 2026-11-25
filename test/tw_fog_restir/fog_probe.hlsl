/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// tests.cpp's probe: the fog local stream's reservoir math (src/games/twattila/dlaa_helper/rt_fog_reservoir.hlsli) on a
// synthetic scene, compiled by dxc (cs_6_5, D3D12 WARP; cs_6_9 statically). Each thread is one trial with its own
// numbers: the helper's stages (candidates; last frame's merged in; three neighbours' merged in, one or two final
// samples) each shaded with an analytic visibility (the plane z = 0 blocks), F x W over the range, so tests.cpp can
// average the trials against a quadrature of (1 / R) x the integral over the ray of every light's light. Controls: the
// candidates without the defensive pick, the neighbours merged with M-share weights instead of Talbot's MIS, and last
// frame merged with its domain's target kept for a light new this trace (as part 1 first built it).
#include "../../src/games/twattila/dlaa_helper/rt_scene.hlsli"

static const uint FOG_SAMPLES = 8u;
#include "../../src/games/twattila/dlaa_helper/rt_fog_reservoir.hlsli"

struct ProbeLight {
  float4 position_reach;
  float4 colour_linear;  // rgb, w 1: linear falloff
};

struct ProbeScenario {
  float4 origin_range[4];  // the four domains: [0] the texel's, [1-3] its neighbours' (last frame's: [0] again)
  float4 direction[4];
  uint4 list_low;
  uint4 list_high;
  uint count;
  uint seed;
  uint occluder;             // 1: the plane z = 0 blocks the segments that cross it
  float history_confidence;  // last frame's M
  float4 view_projection[4];  // CameraOf's check
  uint4 history_low;          // last frame's list (the lights it had: the list without new_light)
  uint4 history_high;
  uint history_count;
  uint new_light;  // the light new this trace (NO_FOG_LIGHT: none)
};

struct ProbeOutput {
  float4 initial;
  float4 temporal;
  float4 spatial;
  float4 spatial_two;
  float4 naive;         // control: M-share weights
  float4 no_defensive;  // control: the list only
  float4 unit;          // FogTarget at (light, t), t, the pack round trip exact (1), FogListOf's size
  float4 camera;        // CameraOf(view_projection)
  float4 as_built;      // control: last frame's domain's target kept for new_light
};

StructuredBuffer<ProbeLight> probe_lights : register(t20);
StructuredBuffer<ProbeScenario> probe_scenario : register(t21);
RWStructuredBuffer<ProbeOutput> probe_outputs : register(u20);

FogLight FogLightOf(uint light) {
  const ProbeLight probe = probe_lights[light];
  FogLight data;
  data.position = probe.position_reach.xyz;
  data.reach = probe.position_reach.w;
  data.colour = probe.colour_linear.rgb;
  data.linear_falloff = probe.colour_linear.w != 0.f;
  data.in_last_trace = light != probe_scenario[0].new_light;
  return data;
}

// A reservoir's F x W over domain 0's range, the plane z = 0 blocking.
float4 Shade(FogDomain domain, FogReservoir reservoir, uint occluder) {
  if (reservoir.light == NO_FOG_LIGHT) return 0.f;
  const FogLight light = FogLightOf(reservoir.light);
  const float3 at = domain.origin + domain.direction * reservoir.t;
  if (occluder != 0u && ((at.z > 0.f) != (light.position.z > 0.f))) return 0.f;
  const float falloff = saturate(1.f - (distance(light.position, at) / max(light.reach, 1e-3f)));
  return float4(light.colour * ((light.linear_falloff ? falloff : falloff * falloff) * reservoir.weight / domain.range), 1.f);
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  const ProbeScenario scenario = probe_scenario[0];
  FogDomain domains[4];
  [unroll]
  for (uint domain = 0u; domain < 4u; ++domain) {
    domains[domain] = FogDomainOf(scenario.origin_range[domain].xyz, scenario.direction[domain].xyz, scenario.origin_range[domain].w);
  }
  const FogList list = FogListOf(scenario.list_low, scenario.list_high, scenario.count);
  const uint2 trial = uint2(id.x & 4095u, id.x >> 12u);
  ProbeOutput output;

  // Each input's own numbers: a frame lane apart.
  FogReservoir spatial[4];
  [unroll]
  for (uint input = 0u; input < 4u; ++input) {
    const uint3 lanes = uint3(trial, scenario.seed * 16u + input);
    spatial[input] = FogCandidates(domains[input], list, scenario.count, Random(uint4(lanes, DIM_FOG_JITTER)), lanes);
  }
  const uint3 lanes = uint3(trial, scenario.seed * 16u);
  output.initial = Shade(domains[0], spatial[0], scenario.occluder);

  // Last frame's (a still camera: domain 0's ray, main's last_trace domain) with its confidence, drawn from last frame's
  // lights; the control keeps its domain's target for new_light.
  {
    FogDomain still[4] = {domains[0], domains[0], domains[0], domains[0]};
    still[1].last_trace = true;
    const uint3 history_lanes = uint3(trial, scenario.seed * 16u + 8u);
    FogReservoir inputs[4] = {spatial[0],
                              FogCandidates(domains[0], FogListOf(scenario.history_low, scenario.history_high, scenario.history_count),
                                            scenario.history_count, Random(uint4(history_lanes, DIM_FOG_JITTER)), history_lanes),
                              EmptyFogReservoir(0.f, 0.f), EmptyFogReservoir(0.f, 0.f)};
    inputs[1].confidence = scenario.history_confidence;
    const float temporal_u = Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_TEMPORAL));
    float4 weights;
    float4 own;
    ResampleFogReservoirs(still, inputs, weights, own);
    output.temporal = Shade(domains[0], ChooseFogReservoir(inputs, weights, own, domains[0].range, temporal_u), scenario.occluder);
    still[1].last_trace = false;
    ResampleFogReservoirs(still, inputs, weights, own);
    output.as_built = Shade(domains[0], ChooseFogReservoir(inputs, weights, own, domains[0].range, temporal_u), scenario.occluder);
  }

  // Three neighbours' (their own rays and ranges), one and two final samples.
  {
    float4 weights;
    float4 own;
    ResampleFogReservoirs(domains, spatial, weights, own);
    output.spatial = Shade(domains[0], ChooseFogReservoir(spatial, weights, own, domains[0].range, Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_SPATIAL))), scenario.occluder);
    output.spatial_two = 0.5f * (output.spatial + Shade(domains[0], ChooseFogReservoir(spatial, weights, own, domains[0].range, Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_SECOND))), scenario.occluder));
    // Control: each input weighted by its share of the confidences, as if every domain reached every sample.
    float4 naive;
    [unroll]
    for (uint input = 0u; input < 4u; ++input) {
      naive[input] = spatial[input].confidence / 4.f * own[input] * spatial[input].weight;
    }
    output.naive = Shade(domains[0], ChooseFogReservoir(spatial, naive, own, domains[0].range, Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_SPATIAL))), scenario.occluder);
  }

  // Control: the list's lights only (no defensive pick).
  output.no_defensive = Shade(domains[0], FogCandidates(domains[0], list, list.size, Random(uint4(lanes, DIM_FOG_JITTER)), lanes), scenario.occluder);

  // Units: FogTarget at a light and a point of domain 0, the pack round trip (the target not kept), the list's size,
  // CameraOf.
  const uint light = id.x % scenario.count;
  const float t = domains[0].range * frac((float)id.x * 0.61803399f);
  FogReservoir packed;
  packed.t = t;
  packed.light = light;
  packed.weight = (float)id.x * 1.37f;
  packed.confidence = (float)(id.x % 37u) * 0.75f;
  packed.target = t * 0.01f;
  packed.range = domains[0].range;
  const FogReservoir unpacked = UnpackFogReservoir(PackFogReservoir(packed));
  const bool exact = unpacked.t == packed.t && unpacked.light == packed.light && unpacked.weight == packed.weight
                     && unpacked.confidence == f16tof32(f32tof16(packed.confidence)) && unpacked.target == 0.f && unpacked.range == packed.range;
  output.unit = float4(FogTarget(light, domains[0].origin + domains[0].direction * t), t, exact ? 1.f : 0.f, (float)list.size);
  output.camera = float4(CameraOf(scenario.view_projection), 0.f);
  probe_outputs[id.x] = output;
}
