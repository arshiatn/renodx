/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The fog local stream of the unified path tracing (rt_fog.cs_6_5.hlsl with FOG_RESTIR; design unified_pt.md 4.6):
// Volumetric ReSTIR (Lin, Wyman and Yuksel 2021, "Fast Volume Rendering with Spatiotemporal Reservoir Resampling",
// [V21]) for one scatter event: x0 the camera, x(t) a point of the fog texel's camera ray, a local light. What a fog
// texel estimates is the light the fog along its ray gets from every local light, averaged over the ray's range R:
// (1 / R) integral over t in [0, R] of the sum over lights of colour x falloff x visibility x smoke (today's FogLightAt
// average, "ratio of sums", but every light, no first-32 cap). A sample is (t, light): t in metres along the ray (kept
// when a neighbour or last frame reuses it [V21 §4.2], a Jacobian of 1), the light's index in this frame's list.
// - Candidates [V21 §3.2, §4.1]: the fog's FOG_SAMPLES squared-spacing points (t = s^2 R, s stratified: p(t) =
//   1 / (2 sqrt(t R)), closed form) and at each point two lights: one from the texel's light list by its target (its
//   odds exact: target / the list's sum there) and, when the list lacks some lights, one of all of them, uniform (the
//   defensive pick: no light that reaches the fog is missed, the R7 hole of a surface tile's list), weighed together by
//   the balance heuristic.
// - Target [V21 Eq. 22-23]: the light's colour's luminance x its falloff at x(t), no visibility and no smoke (the cheap
//   T~); the final shading alone traces them [V21 Eq. 26, §5.1].
// - Reuse [V21 §4.3-4.4]: Talbot's MIS [V21 Eq. 11] over up to four domains (this texel's ray, last frame's texel or
//   three neighbours'), each a sample's target along that domain's ray at the same t, confidences M.
// The includer gives FOG_SAMPLES, FogLightOf (below) and rt_scene.hlsli's Random and Luminance (test/tw_fog_restir's
// probe gives its own lights).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_FOG_RESERVOIR_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_FOG_RESERVOIR_HLSLI_

struct FogLight {
  float3 position;
  float reach;  // metres
  float3 colour;
  bool linear_falloff;  // 1 - distance / reach; else its square
};

FogLight FogLightOf(uint light);  // the includer's

static const uint NO_FOG_LIGHT = 0xFFu;
// The fog local stream's numbers: DIM_FOG_LIGHT's range (rt_scene.hlsli; its per-point picks 0-7 as FogLightAt's),
// unused by FogLightAt in the FOG_RESTIR builds.
static const uint FOG_DIM_UNIFORM = 8u;      // a point's defensive pick (+ point)
static const uint FOG_DIM_STREAM = 16u;      // the candidates into the reservoir
static const uint FOG_DIM_TEMPORAL = 17u;    // last frame's reservoir merged
static const uint FOG_DIM_NEIGHBOURS = 18u;  // the neighbours' offsets
static const uint FOG_DIM_SPATIAL = 19u;     // the neighbours merged: the first final sample
static const uint FOG_DIM_SECOND = 20u;      // the second final sample (FOG_TWO_FINAL)

// One of a fog texel's camera rays: the domain of its samples.
struct FogDomain {
  float3 origin;
  float3 direction;
  float range;  // metres (0: none)
};

struct FogReservoir {
  float t;           // metres along the ray
  uint light;        // NO_FOG_LIGHT: none chosen
  float weight;      // W, the chosen sample's unbiased contribution weight (its target's inverse x the weights' sum)
  float confidence;  // M (1 a frame's candidates)
  float target;      // the chosen sample's target in its domain
  float range;       // its domain's range when written
};

FogReservoir EmptyFogReservoir(float confidence, float range) {
  FogReservoir reservoir;
  reservoir.t = 0.f;
  reservoir.light = NO_FOG_LIGHT;
  reservoir.weight = 0.f;
  reservoir.confidence = confidence;
  reservoir.target = 0.f;
  reservoir.range = range;
  return reservoir;
}

// 16 bytes (R32G32B32A32_UINT): t and W as floats, the light and M (f16), the target and the range (f16).
uint4 PackFogReservoir(FogReservoir reservoir) {
  return uint4(asuint(reservoir.t), asuint(reservoir.weight), reservoir.light | (f32tof16(reservoir.confidence) << 16),
               f32tof16(reservoir.target) | (f32tof16(reservoir.range) << 16));
}

FogReservoir UnpackFogReservoir(uint4 packed) {
  FogReservoir reservoir;
  reservoir.t = asfloat(packed.x);
  reservoir.weight = asfloat(packed.y);
  reservoir.light = packed.z & 0xFFu;
  reservoir.confidence = f16tof32(packed.z >> 16);
  reservoir.target = f16tof32(packed.w);
  reservoir.range = f16tof32(packed.w >> 16);
  return reservoir;
}

// The target of `light` at a point of the fog: its colour's luminance x its falloff there (no visibility, no smoke).
float FogTarget(uint light, float3 at) {
  const FogLight data = FogLightOf(light);
  const float falloff = saturate(1.f - (distance(data.position, at) / max(data.reach, 1e-3f)));
  return max(Luminance(data.colour), 0.f) * (data.linear_falloff ? falloff : (falloff * falloff));
}

// A sample's target in a domain: along its ray at the same t, 0 past its range.
float FogDomainTarget(FogDomain domain, float t, uint light) {
  if (light == NO_FOG_LIGHT || t > domain.range) return 0.f;
  return FogTarget(light, domain.origin + (domain.direction * t));
}

// A fog texel's candidate lights: up to 32 of this frame's lights (rt_lights' tile list, a byte each, NO_FOG_LIGHT past
// the end), their count and the 128-bit set of them.
struct FogList {
  uint4 low;   // slots 0-15
  uint4 high;  // slots 16-31
  uint size;
  uint4 lights;  // bit i: light i is in the list
};

uint FogListLight(FogList list, uint slot) {
  // Selected, not indexed (a vector indexed by a variable goes through local memory).
  const uint4 words = (slot < 16u) ? list.low : list.high;
  const uint lane = (slot >> 2u) & 3u;
  return (((lane == 0u) ? words.x : (lane == 1u) ? words.y : (lane == 2u) ? words.z : words.w) >> ((slot & 3u) * 8u)) & 0xFFu;
}

bool FogListHolds(FogList list, uint light) {
  const uint word = light >> 5u;
  return ((((word == 0u) ? list.lights.x : (word == 1u) ? list.lights.y : (word == 2u) ? list.lights.z : list.lights.w) >> (light & 31u)) & 1u) != 0u;
}

// The list of the words' bytes up to the first NO_FOG_LIGHT, at most `limit`.
FogList FogListOf(uint4 low, uint4 high, uint limit) {
  FogList list;
  list.low = low;
  list.high = high;
  list.size = 0u;
  list.lights = 0u;
  [loop]
  for (uint slot = 0u; slot < min(limit, 32u); ++slot) {
    const uint light = FogListLight(list, slot);
    if (light == NO_FOG_LIGHT) break;
    list.size = slot + 1u;
    const uint word = light >> 5u;
    const uint bit = 1u << (light & 31u);
    list.lights |= uint4((word == 0u) ? bit : 0u, (word == 1u) ? bit : 0u, (word == 2u) ? bit : 0u, (word == 3u) ? bit : 0u);
  }
  return list;
}

// Streams a candidate of resampling weight `weight` into the running sum `total`; `u` (in [0, 1)) picks with one number
// for all of them (rescaled each time, as FogLightAt's pick). Whether it was chosen.
bool StreamFogCandidate(float weight, inout float total, inout float u) {
  if (weight <= 0.f) return false;
  total += weight;
  const float keep = weight / total;
  if (u < keep) {
    u /= keep;
    return true;
  }
  u = (u - keep) / (1.f - keep);
  return false;
}

// This frame's candidates of a texel's ray [V21 §3.3, Eq. 20-21 with a closed-form distance pdf, §4.1]: at the
// FOG_SAMPLES points (s = (point + jitter) / FOG_SAMPLES, t = s^2 range) the list's pick and the defensive pick when the
// list lacks some of the `count` lights. A candidate's weight is target / (FOG_SAMPLES p(t) (p_list + p_uniform)).
// `lanes`: Random's (x, y, frame).
FogReservoir FogCandidates(FogDomain domain, FogList list, uint count, float jitter, uint3 lanes) {
  FogReservoir reservoir = EmptyFogReservoir(1.f, domain.range);
  const bool defensive = list.size < count;
  const float even = defensive ? (1.f / (float)count) : 0.f;
  float total = 0.f;
  float u = Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_STREAM));
  [loop]
  for (uint index = 0u; index < FOG_SAMPLES; ++index) {
    const float along = (index + jitter) / FOG_SAMPLES;
    const float t = along * along * domain.range;
    const float3 at = domain.origin + (domain.direction * t);
    // 1 / (FOG_SAMPLES p(t)): p(t) = 1 / (2 sqrt(t range)) = 1 / (2 along range).
    const float spacing = 2.f * along * domain.range / FOG_SAMPLES;
    float pick = Random(uint4(lanes, DIM_FOG_LIGHT + index));
    float sum = 0.f;
    uint picked = NO_FOG_LIGHT;
    float picked_target = 0.f;
    [loop]
    for (uint slot = 0u; slot < list.size; ++slot) {
      const uint light = FogListLight(list, slot);
      const float target = FogTarget(light, at);
      if (StreamFogCandidate(target, sum, pick)) {
        picked = light;
        picked_target = target;
      }
    }
    if (picked != NO_FOG_LIGHT
        && StreamFogCandidate(picked_target / ((picked_target / sum) + even) * spacing, total, u)) {
      reservoir.t = t;
      reservoir.light = picked;
      reservoir.target = picked_target;
    }
    if (defensive) {
      const uint light = min((uint)(Random(uint4(lanes, DIM_FOG_LIGHT + FOG_DIM_UNIFORM + index)) * (float)count), count - 1u);
      const float target = FogTarget(light, at);
      if (target > 0.f
          && StreamFogCandidate(target / ((FogListHolds(list, light) ? (target / sum) : 0.f) + even) * spacing, total, u)) {
        reservoir.t = t;
        reservoir.light = light;
        reservoir.target = target;
      }
    }
  }
  reservoir.weight = (reservoir.light != NO_FOG_LIGHT) ? total / reservoir.target : 0.f;
  return reservoir;
}

// Up to four reservoirs merged [V21 §4.3-4.4] into domain 0's (inputs[i] from domains[i]; an unused one M 0): input i's
// resampling weight m_i(y_i) target_0(y_i) W_i, with Talbot's MIS m_i(y) = M_i target_i(y) / sum_j M_j target_j(y)
// [V21 Eq. 11] (the generalized balance heuristic: a partition of unity over the domains that reach y, so unbiased for any
// targets). `own`: each input's target in domain 0.
void ResampleFogReservoirs(FogDomain domains[4], FogReservoir inputs[4], out float4 weights, out float4 own) {
  [unroll]
  for (uint input = 0u; input < 4u; ++input) {
    float all = 0.f;
    float its = 0.f;
    float first = 0.f;
    [unroll]
    for (uint domain = 0u; domain < 4u; ++domain) {
      const float target = (inputs[domain].confidence > 0.f) ? FogDomainTarget(domains[domain], inputs[input].t, inputs[input].light) : 0.f;
      all += inputs[domain].confidence * target;
      if (domain == input) its = target;
      if (domain == 0u) first = target;
    }
    weights[input] = (all > 0.f) ? (inputs[input].confidence * its / all) * first * inputs[input].weight : 0.f;
    own[input] = first;
  }
}

// One sample of the merged inputs by their weights (`u` in [0, 1)), its W in domain 0 (the weights' sum over its target
// there), M the inputs' sum, the range domain 0's. Twice with independent numbers: two final samples of one distribution.
FogReservoir ChooseFogReservoir(FogReservoir inputs[4], float4 weights, float4 own, float range, float u) {
  FogReservoir chosen = EmptyFogReservoir(inputs[0].confidence + inputs[1].confidence + inputs[2].confidence + inputs[3].confidence, range);
  float total = 0.f;
  [unroll]
  for (uint input = 0u; input < 4u; ++input) {
    if (StreamFogCandidate(weights[input], total, u)) {
      chosen.t = inputs[input].t;
      chosen.light = inputs[input].light;
      chosen.target = own[input];
    }
  }
  chosen.weight = (chosen.light != NO_FOG_LIGHT && chosen.target > 0.f) ? total / chosen.target : 0.f;
  if (chosen.weight == 0.f) chosen.light = NO_FOG_LIGHT;
  return chosen;
}

// The camera's position by its registers (clip = (dot(position, [0]), ...)): where clip x, y and w are 0 (rows 0, 1, 3),
// by Cramer's rule. Last frame's for the temporal domain.
float3 CameraOf(float4 registers[4]) {
  const float3 a = registers[0].xyz;
  const float3 b = registers[1].xyz;
  const float3 c = registers[3].xyz;
  const float3 b_c = cross(b, c);
  const float determinant = dot(a, b_c);
  return -(registers[0].w * b_c + registers[1].w * cross(c, a) + registers[3].w * cross(a, b)) / determinant;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_FOG_RESERVOIR_HLSLI_
