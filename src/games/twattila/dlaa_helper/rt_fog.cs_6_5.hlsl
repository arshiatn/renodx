/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The Total War ray-traced fog shadows (dlaa_helper/rt_d3d12.hpp, bridge v11): per fog texel (a quarter of the render
// pixels each way) the sun's visibility along the camera's ray to the texel's surface: FOG_SAMPLES points, closer
// together near the camera (where shafts show; each weighted by the stretch it stands for), each one ray toward the
// sun (dimmed by the smoke it meets). The games' fog pass (twattila/rt_fog.hlsli) dims the sun's share of its colour by
// it: light shafts through gaps, the fog in shadow darker. A sky texel marches its camera ray FOG_RANGE far, as a
// surface beyond it does. Frames without a sun: 1. Noise moves every frame (DLAA / DLSS settle it). With FOG_LIGHTS (v18)
// the local lights' light at the same points into fog_lights (rgb): torches and fires glowing in the fog, their shafts
// and shadows too (rt_fog.hlsli adds it to the fog's colour).
// With FOG_RESTIR (the unified path tracing's fog streams, U6; design unified_pt.md 4.6) the local lights' light comes
// from Volumetric ReSTIR (rt_fog_reservoir.hlsli) in two dispatches: main, the sun as above (with Steady Fog Light
// Shafts its history) and the local stream's candidates merged with last frame's reservoir, no light rays; then shade
// (after a UAV barrier), three neighbours' reservoirs merged in and one or two chosen samples a texel shaded with their
// shadow rays and smoke (8 light rays a texel -> 1 or 2).
// Build (build.ps1): dxc -T cs_6_5 -Fh rt_fog.h -Vn RT_FOG; with -T cs_6_9 -D OPACITY_MICROMAPS -Vn RT_FOG_OMM; with
// -D FOG_RESTIR -E main -Vn RT_FOG_RESTIR and -E shade -Vn RT_FOG_RESTIR_SHADE (both with their OMM builds too).
#include "./rt_scene.hlsli"
#include "./rt_camera.hlsli"

RWTexture2D<unorm float> fog : register(u27);
RWTexture2D<float4> fog_lights : register(u34);

// The local lights' light at a point of the fog: one light picked by what it gives there (its colour's brightness times
// its shader's falloff, rt_scene.hlsli LightLinear; reservoir sampling with one number), weighted back by its odds,
// through a shadow ray that the smoke it meets dims.
float3 FogLightAt(float3 at, float u) {
  float total = 0.f;
  uint picked = 32u;
  float picked_weight = 0.f;
  [loop]
  for (uint light = 0u; light < min(light_count, 32u); ++light) {
    const float falloff = saturate(1.f - (distance(lights[light].xyz, at) / max(lights[light].w, 1.0002842e-3f)));
    const float weight = dot(light_colours[light].rgb, float3(0.2126f, 0.7152f, 0.0722f))
                         * (LightLinear(light) ? falloff : (falloff * falloff));
    if (weight <= 0.f) continue;
    total += weight;
    const float keep = weight / total;
    if (u < keep) {
      picked = light;
      picked_weight = weight;
      u /= keep;
    } else {
      u = (u - keep) / (1.f - keep);
    }
  }
  if (picked == 32u) return 0.f;
  const float3 to_light = lights[picked].xyz - at;
  const float light_distance = length(to_light);
  const float reach = light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance);
  if (light_distance <= 1e-3f || Occluded(at, to_light / light_distance, reach, NO_GRASS_SMOKE)) {
    return 0.f;
  }
  const float falloff = saturate(1.f - (light_distance / max(lights[picked].w, 1e-3f)));
  // The smoke apart (rt_scene.hlsli SmokeTransmittance): a fog sample's ray counts 2 that dim it, 4 met at most (its rays
  // are as many as the sun's; here, not along the quads' normal, the chord's share is only near enough).
  if (LightLinear(picked)) {
    return light_colours[picked].rgb * (falloff * total / picked_weight * SmokeTransmittance(at, to_light / light_distance, reach, 2u));
  }
  return light_colours[picked].rgb
         * (falloff * falloff * total / picked_weight * SmokeTransmittance(at, to_light / light_distance, reach, 2u));
}

static const uint FOG_SAMPLES = 8u;
static const float FOG_RANGE = 400.f;      // metres along the camera's ray at most
static const float FOG_SUN_RANGE = 1000.f;  // metres toward the sun

// A fog texel's camera ray from `camera` through its `pixel` (min(texel x 4 + 2, the screen's last)) at `position`: its
// direction, its range (metres; 0: none). The surface there, or a sky texel's FOG_RANGE.
void FogRay(uint2 pixel, float4 position, float3 camera, out float3 direction, out float range) {
  if (position.w > 0.f) {
    const float surface_distance = distance(position.xyz, camera);
    direction = (position.xyz - camera) / max(surface_distance, 1.0003276e-3f);
    range = min(surface_distance, FOG_RANGE);
  } else {
    // The sky's camera ray through the texel's pixel: the line through the camera where its clip planes (x = ndc.x w,
    // y = ndc.y w) meet, toward w > 0. Sky texels got no glow and no shafts while the hills a km away beside them got the
    // first FOG_RANGE's (the horizon lighter than the sky, DEBUG44 (d)). No camera registers: the sky as before (sun 1).
    const float2 ndc = (float2(pixel) + 0.5f) / float2(width, height) * float2(2.f, -2.f) + float2(-1.f, 1.f);
    const float3 along = cross(view_projection[0].xyz - ndc.x * view_projection[3].xyz, view_projection[1].xyz - ndc.y * view_projection[3].xyz);
    const float along_length = length(along);
    direction = along / max(along_length, 1e-30f) * ((dot(along, view_projection[3].xyz) < 0.f) ? -1.f : 1.f);
    range = (along_length > 0.f) ? FOG_RANGE : 0.f;
  }
}

#if defined(FOG_RESTIR)
// The fog streams' switches (the fog dispatches' root constants; rt_d3d12.hpp sets them).
cbuffer FogRestir : register(b2) {
  uint fog_restir_flags;
};
// FOG_HISTORY: fog_reservoirs[1] holds the last trace's final reservoirs (its shade ran, this fog size), and b4's
// light_previous maps this trace's lights to that trace's list (FogLightNow, FogLightOf's in_last_trace) and b3's
// previous_view_projection is that trace's camera (FogTexelLastFrame, CameraOf).
static const uint FOG_HISTORY = 1u;
// FOG_TILES: rt_lights wrote this trace's tile lists (light_mask from light_tile_row), a UAV barrier on light_mask
// between it and main.
static const uint FOG_TILES = 2u;
static const uint FOG_TWO_FINAL = 4u;    // two final samples a texel, else one (S7: the 4 x 4 block-flicker metric picks)
static const uint FOG_SUN_HISTORY = 8u;  // Steady Fog Light Shafts (RayTracedFogHistory, Q4): the sun's share kept over frames
static const uint FOG_SUN_PARITY = 16u;  // the last trace's sun history in fog_sun_history[1] (else [0]); this one's the other
// FOG_SUN_VALID: that history is the last trace's (this fog size) and previous_view_projection that trace's camera.
static const uint FOG_SUN_VALID = 32u;

// The reservoirs at the fog texels (R32G32B32A32_UINT, rt_fog_reservoir.hlsli PackFogReservoir): [0] this frame's
// candidates merged with last frame's (main writes, shade reads), [1] the final ones (shade writes, the next main reads).
RWTexture2D<uint4> fog_reservoirs[2] : register(u85);
// The sun's share kept (R32_UINT, by FOG_SUN_PARITY): unorm16, its frames (bits 16-19, 0: none), its ray's range (f16's
// bits 4-14 at bits 20-30).
RWTexture2D<uint> fog_sun_history[2] : register(u87);
RWTexture2D<uint> light_mask : register(u22);  // rt_lights' tile lists (rt_lights.cs_6_5.hlsl TileWord)

static const float FOG_HISTORY_CAP = 4.f;  // Q [V21 §5.3]: last frame's confidence (and the sun's frames) at most 4 frames'
// A reprojected point farther from last frame's camera than its texel's range by more than this share + 0.25 m: that
// texel's ray did not reach it (it saw something nearer).
static const float FOG_HISTORY_REACH = 0.05f;

#include "./rt_fog_reservoir.hlsli"

// Light `light`'s place in the last trace's list (light_previous's byte, selected, not indexed; 255: new this trace).
uint LastTracePlace(uint light) {
  const uint4 words = light_previous[light >> 4u];
  const uint lane = (light >> 2u) & 3u;
  return (((lane == 0u) ? words.x : (lane == 1u) ? words.y : (lane == 2u) ? words.z : words.w) >> ((light & 3u) * 8u)) & 0xFFu;
}

FogLight FogLightOf(uint light) {
  FogLight data;
  data.position = lights[light].xyz;
  data.reach = lights[light].w;
  data.colour = light_colours[light].rgb;
  data.linear_falloff = LightLinear(light);
  data.in_last_trace = LastTracePlace(light) != 255u;
  return data;
}

// Local light `light`'s light at a point of the fog through a shadow ray that the smoke it meets dims (FogLightAt's after
// its pick): a final sample's F [V21 Eq. 26].
float3 FogLightThrough(uint light, float3 at) {
  const float3 to_light = lights[light].xyz - at;
  const float light_distance = length(to_light);
  const float reach = light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance);
  if (light_distance <= 1e-3f || Occluded(at, to_light / light_distance, reach, NO_GRASS_SMOKE)) {
    return 0.f;
  }
  const float falloff = saturate(1.f - (light_distance / max(lights[light].w, 1e-3f)));
  return light_colours[light].rgb
         * ((LightLinear(light) ? falloff : (falloff * falloff)) * SmokeTransmittance(at, to_light / light_distance, reach, 2u));
}

// Where last frame's camera saw `world`: its fog texel (false: behind it or off its screen).
bool FogTexelLastFrame(float3 world, out uint2 texel) {
  texel = 0u;
  const float4 clip = ClipOf(world, previous_view_projection);
  const float2 uv = clip.xy / max(clip.w, 9.998928e-7f) * float2(0.5f, -0.5f) + 0.5f;
  if (clip.w <= 0.f || any(uv < 0.f) || any(uv >= 1.f)) return false;
  texel = min(uint2(uv * float2(width, height)), uint2(width, height) - 1u) / 4u;
  return true;
}

// The light now at last trace's place `previous` in the list (light_previous's inverse; NO_FOG_LIGHT: gone).
uint FogLightNow(uint previous, uint count) {
  [loop]
  for (uint light = 0u; light < count; ++light) {
    if (LastTracePlace(light) == previous) return light;
  }
  return NO_FOG_LIGHT;
}

// The neighbours within 10 pixels [V21 §5.3] (2.5 fog texels), in an order whose stride 7 spreads three picks around.
static const int2 FOG_NEIGHBOURS[20] = {
    int2(1, 0), int2(-2, 1), int2(0, -1), int2(1, 2), int2(-1, 0), int2(2, -1), int2(0, 1), int2(-1, -2), int2(1, 1), int2(-2, 0),
    int2(-1, 1), int2(2, 1), int2(1, -1), int2(0, 2), int2(-1, -1), int2(-2, -1), int2(2, 0), int2(-1, 2), int2(0, -2), int2(1, -2)};
#endif

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (id.x >= (width + 3u) / 4u || id.y >= (height + 3u) / 4u) return;
  const uint2 pixel = min(id.xy * 4u + 2u, uint2(width, height) - 1u);
  const float4 position = positions[pixel];
  const float4 toward_sun = sun[uint2(0, 0)];
  const float3 camera = sun[uint2(1, 0)].xyz;
  float3 direction;
  float range;
  FogRay(pixel, position, camera, direction, range);
  const bool glowing = (flags & FOG_LIGHTS) != 0u && light_count != 0u && range > 0.f;
  if (!glowing && (flags & FOG_LIGHTS) != 0u) fog_lights[id.xy] = 0.f;
  if (range <= 0.f || (toward_sun.w == 0.f && !glowing)) {
    fog[id.xy] = 1.f;
#if defined(FOG_RESTIR)
    if ((fog_restir_flags & FOG_SUN_HISTORY) != 0u) fog_sun_history[((fog_restir_flags & FOG_SUN_PARITY) != 0u) ? 0u : 1u][id.xy] = 0u;
#endif
    return;
  }
  const float jitter = Random(uint4(id.xy, frame, DIM_FOG_JITTER));
  float lit = 0.f;
#if !defined(FOG_RESTIR)
  float3 glow = 0.f;
#endif
  float weights = 0.f;
  [loop]
  for (uint index = 0u; index < FOG_SAMPLES; ++index) {
    // Squared spacing: a point at `along` stands for a stretch in proportion to it.
    const float along = (index + jitter) / FOG_SAMPLES;
    const float3 at = camera + direction * (along * along * range);
    if (toward_sun.w != 0.f && !Occluded(at, toward_sun.xyz, FOG_SUN_RANGE, NO_GRASS_SMOKE)) {
      lit += along * SmokeTransmittance(at, toward_sun.xyz, FOG_SUN_RANGE, 2u);
    }
#if !defined(FOG_RESTIR)
    if (glowing) glow += along * FogLightAt(at, Random(uint4(id.xy, frame, DIM_FOG_LIGHT + index)));
#endif
    weights += along;
  }
#if defined(FOG_RESTIR)
  float shafts = (weights > 0.f && toward_sun.w != 0.f) ? lit / weights : 1.f;
  const float3 previous_camera = CameraOf(previous_view_projection);
  if ((fog_restir_flags & FOG_SUN_HISTORY) != 0u) {
    // Steady Fog Light Shafts: blended into the history of the texel where last frame's camera saw this ray's end, if
    // its ray ended there too (the same column of fog), at most FOG_HISTORY_CAP frames kept [V21 §4.4]; else a new one.
    const uint written = ((fog_restir_flags & FOG_SUN_PARITY) != 0u) ? 0u : 1u;
    const float3 end = camera + direction * range;
    uint frames = 0u;
    uint2 last;
    if ((fog_restir_flags & FOG_SUN_VALID) != 0u && FogTexelLastFrame(end, last)) {
      const uint history = fog_sun_history[1u - written][last];
      const float history_range = f16tof32(((history >> 20u) & 0x7FFu) << 4u);
      frames = (history >> 16u) & 15u;
      if (frames != 0u && abs(distance(end, previous_camera) - history_range) <= history_range * FOG_HISTORY_REACH + 0.25f) {
        shafts = lerp((float)(history & 0xFFFFu) / 65535.f, shafts, 1.f / (min((float)frames, FOG_HISTORY_CAP) + 1.f));
      } else {
        frames = 0u;
      }
    }
    fog_sun_history[written][id.xy] = (uint)round(saturate(shafts) * 65535.f) | (min(frames + 1u, 15u) << 16u) | ((f32tof16(range) >> 4u) << 20u);
  }
  fog[id.xy] = shafts;
  if (glowing) {
    // The local stream: this frame's candidates [V21 §3.3, §4.1] from the texel's pixel's tile list (else the first 32
    // lights, the lights' own order without tiles), merged with last frame's final reservoir [V21 §4.4].
    const uint count = min(light_count, 128u);
    uint4 low = uint4(0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu);
    uint4 high = low + 0x10101010u;
    if ((fog_restir_flags & FOG_TILES) != 0u && light_tile_row != 0u) {
      const uint2 first = uint2((pixel.x / 8u) * 4u, light_tile_row + (pixel.y / 8u) * 2u);
      low = uint4(light_mask[first], light_mask[first + uint2(1u, 0u)], light_mask[first + uint2(2u, 0u)], light_mask[first + uint2(3u, 0u)]);
      high = uint4(light_mask[first + uint2(0u, 1u)], light_mask[first + uint2(1u, 1u)], light_mask[first + uint2(2u, 1u)], light_mask[first + uint2(3u, 1u)]);
    }
    FogDomain domains[4];
    FogReservoir inputs[4];
    domains[0] = FogDomainOf(camera, direction, range);
    inputs[0] = FogCandidates(domains[0], FogListOf(low, high, count), count, jitter, uint3(id.xy, frame));
    [unroll]
    for (uint unused = 1u; unused < 4u; ++unused) {
      domains[unused] = domains[0];
      inputs[unused] = EmptyFogReservoir(0.f, 0.f);
    }
    // Last frame's: the texel its camera saw this frame's chosen point in (the ray's end without one) [V21 §4.4], if that
    // texel's ray reached it; its light renumbered, its confidence capped. Its domain: last frame's camera's ray through
    // the point (its texel's, up to the texel's size), its range, its target 0 for lights new this trace. Under motion
    // the texel follows the candidates' chosen point, so which domain merges depends on that sample: a second bias beside
    // the domain's approximation (none with a still camera; the lag metric judges both, the ray's end the fixed choice).
    const float3 chosen = camera + direction * ((inputs[0].light != NO_FOG_LIGHT) ? inputs[0].t : range);
    uint2 last;
    if ((fog_restir_flags & FOG_HISTORY) != 0u && FogTexelLastFrame(chosen, last)) {
      FogReservoir history = UnpackFogReservoir(fog_reservoirs[1][last]);
      const float3 from_previous = chosen - previous_camera;
      const float previous_distance = length(from_previous);
      if (history.confidence > 0.f && previous_distance <= history.range * (1.f + FOG_HISTORY_REACH) + 0.25f) {
        if (history.light != NO_FOG_LIGHT) history.light = FogLightNow(history.light, count);
        history.confidence = min(history.confidence, FOG_HISTORY_CAP);
        inputs[1] = history;
        domains[1] = FogDomainOf(previous_camera, (previous_distance > 1e-3f) ? from_previous / previous_distance : direction, history.range);
        domains[1].last_trace = true;
      }
    }
    float4 resampling;
    float4 own;
    ResampleFogReservoirs(domains, inputs, resampling, own);
    fog_reservoirs[0][id.xy] = PackFogReservoir(ChooseFogReservoir(inputs, resampling, own, range, Random(uint4(id.xy, frame, DIM_FOG_LIGHT + FOG_DIM_TEMPORAL))));
  }
#else
  fog[id.xy] = (weights > 0.f && toward_sun.w != 0.f) ? lit / weights : 1.f;
  if (glowing) fog_lights[id.xy] = float4((weights > 0.f) ? glow / weights : 0.f, 0.f);
#endif
}

#if defined(FOG_RESTIR)
// The local stream's reuse and shading: this texel's reservoir merged with three neighbours' [V21 §4.3, §5.3] into the
// final one (fog_reservoirs[1], next frame's history), its chosen sample (with FOG_TWO_FINAL two of the same
// distribution, independent numbers) shaded with its shadow ray and smoke [V21 Eq. 26]: F x W over the ray's range.
[numthreads(8, 8, 1)]
void shade(uint3 id : SV_DispatchThreadID) {
  const uint2 size = (uint2(width, height) + 3u) / 4u;
  if (id.x >= size.x || id.y >= size.y) return;
  const float3 camera = sun[uint2(1, 0)].xyz;
  const uint start = min((uint)(Random(uint4(id.xy, frame, DIM_FOG_LIGHT + FOG_DIM_NEIGHBOURS)) * 20.f), 19u);
  FogDomain domains[4];
  FogReservoir inputs[4];
  [unroll]
  for (uint input = 0u; input < 4u; ++input) {
    const uint2 texel = (input == 0u) ? id.xy : (uint2)clamp(int2(id.xy) + FOG_NEIGHBOURS[(start + (input - 1u) * 7u) % 20u], 0, int2(size) - 1);
    const uint2 pixel = min(texel * 4u + 2u, uint2(width, height) - 1u);
    float3 ray;
    float ray_range;
    FogRay(pixel, positions[pixel], camera, ray, ray_range);
    domains[input] = FogDomainOf(camera, ray, ray_range);
    // A neighbour clamped onto this texel (the screen's edge) counts once.
    inputs[input] = EmptyFogReservoir(0.f, 0.f);
    if (domains[input].range > 0.f && (input == 0u || any(texel != id.xy))) inputs[input] = UnpackFogReservoir(fog_reservoirs[0][texel]);
  }
  if ((flags & FOG_LIGHTS) == 0u || light_count == 0u || domains[0].range <= 0.f) {
    fog_reservoirs[1][id.xy] = PackFogReservoir(EmptyFogReservoir(0.f, 0.f));
    return;
  }
  float4 resampling;
  float4 own;
  ResampleFogReservoirs(domains, inputs, resampling, own);
  const FogReservoir chosen = ChooseFogReservoir(inputs, resampling, own, domains[0].range, Random(uint4(id.xy, frame, DIM_FOG_LIGHT + FOG_DIM_SPATIAL)));
  fog_reservoirs[1][id.xy] = PackFogReservoir(chosen);
  float3 light = (chosen.light != NO_FOG_LIGHT) ? FogLightThrough(chosen.light, camera + domains[0].direction * chosen.t) * chosen.weight : 0.f;
  if ((fog_restir_flags & FOG_TWO_FINAL) != 0u) {
    // The same sample twice (the same t and light, so the same W): its F again, not its ray.
    const FogReservoir second = ChooseFogReservoir(inputs, resampling, own, domains[0].range, Random(uint4(id.xy, frame, DIM_FOG_LIGHT + FOG_DIM_SECOND)));
    if (second.light != chosen.light || second.t != chosen.t) {
      light = 0.5f * (light + ((second.light != NO_FOG_LIGHT) ? FogLightThrough(second.light, camera + domains[0].direction * second.t) * second.weight : 0.f));
    }
  }
  fog_lights[id.xy] = float4(light / domains[0].range, 0.f);
}
#endif
