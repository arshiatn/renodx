/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Path-Traced Torch Light's passes (PT DI step 4c, the PATH_DI builds: rt_di_initial / rt_di_temporal / rt_di_spatial /
// rt_di_shade): the light surface, the pass constants and the game's light, included after rt_scene.hlsli,
// rt_camera.hlsli and rt_light_tiles.hlsli and before rt_rtxdi_bridge.hlsli (whose PATH_DI bodies call these).
// A DI pixel (rt_lights' LIGHTS_DI build): a surface whose tile lists more than LIGHT_RAYS_ALWAYS lights it reaches; its
// mask word 0 (each listed light dark in the game's shaders), its light history's word DI_PIXEL_WORD. The passes light
// it with RTXDI's pick of its tile's listed lights (the integrand: each listed light's game light times its visibility;
// an unlisted light keeps the game's unshadowed light, so it is no candidate: its target 0).
#ifndef SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_SURFACE_HLSLI_
#define SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_SURFACE_HLSLI_

#include "../tw_point_light.hlsli"

RWTexture2D<uint> light_mask : register(u22);
RWTexture2D<uint> light_history[2] : register(u32);
// The addon's light surface (bridge v32 rt_light_surface, RGBA32_UINT at the render pixels): x, y, z the G-buffer's
// channels 1-3 as RGBA8 (r in the low byte), w's low byte the pixel's light model (stencil & 3: TW_POINT_LIGHT_SLM or
// TW_POINT_LIGHT_SKIN; else no point light lights it). Last trace's: the helper's copy.
RWTexture2D<uint4> light_surface : register(u78);
RWTexture2D<uint4> light_surface_previous : register(u79);

// The DI passes' constants (b2, rt_d3d12.hpp DiConstants).
cbuffer DiPass : register(b2) {
  uint di_block_row_pitch;  // RTXDI_ReservoirBufferParameters (RTXDI's CalculateReservoirBufferParameters)
  uint di_array_pitch;
  uint di_flags;           // DI_*
  uint di_uniform_random;  // the frame's number for the temporal pass's permutation
  uint di_offset_mask;     // the neighbour offsets' count - 1
};
static const uint DI_HISTORY = 1u;  // the last trace's reservoirs (DI_SHADE_ARRAY), light surface and light places are valid
// Test switches (RENODX_TW_DI_TEST, rt_d3d12.hpp): the bias correction (0 ray traced, 1 basic, 2 pairwise, 3 off) at
// bits 1-2, no temporal pass, no spatial pass.
static const uint DI_BIAS_SHIFT = 1u;
static const uint DI_NO_TEMPORAL = 8u;
static const uint DI_NO_SPATIAL = 16u;

// The reservoirs' arrays: the initial pass's, the temporal pass's, the spatial pass's (the shading's and the next
// trace's history).
static const uint DI_INITIAL_ARRAY = 0u;
static const uint DI_TEMPORAL_ARRAY = 1u;
#if defined(DI_FUSED)
// The fused variant (rt_di_spatiotemporal; -D DI_FUSED in every DI pass build of it): it reads the last trace's shading
// reservoirs while it writes this trace's, so the two take arrays 1 and 2 in turns by the light history's parity
// (DiParity flips every trace; the history is read only after a flip: DI_HISTORY needs the trace the frame before).
#define DI_SHADE_ARRAY (1u + DiParity())
#define DI_HISTORY_ARRAY (2u - DiParity())
#else
static const uint DI_SHADE_ARRAY = 2u;
// The last trace's shading reservoirs, the temporal reuse's history: the split passes' the same array (the temporal pass
// reads it before the spatial pass writes it).
static const uint DI_HISTORY_ARRAY = DI_SHADE_ARRAY;
#endif

// The light history's set this trace writes (rt_lights' parity_written); the last trace's is the other.
uint DiParity() {
  return ((path_flags & PATH_AMBIENT_PARITY) != 0u) ? 0u : 1u;
}

// The game's point-light surface at a pixel from its light surface texel.
TwPointLightSurface DiSurfaceOf(uint4 texel, float3 position) {
  TwPointLightSurface surface;
  surface.channel_1 = float4(texel.x & 0xFFu, (texel.x >> 8u) & 0xFFu, (texel.x >> 16u) & 0xFFu, texel.x >> 24u) / 255.f;
  surface.channel_2 = float4(texel.y & 0xFFu, (texel.y >> 8u) & 0xFFu, (texel.y >> 16u) & 0xFFu, texel.y >> 24u) / 255.f;
  surface.channel_3 = float4(texel.z & 0xFFu, (texel.z >> 8u) & 0xFFu, (texel.z >> 16u) & 0xFFu, texel.z >> 24u) / 255.f;
  surface.position = position;
  surface.model = texel.w & 0xFFu;
  return surface;
}

// The game's light of local light `light` at a light surface (its point-light shader with visibility 1, at the light's
// centre; SSAO left out: the addon's composite multiplies it into the slm light, rt_di_shade). The DI passes' one
// evaluation of it: the target (rt_rtxdi_bridge.hlsli) and the shading both call this. Highdef only: the addon leaves
// the lowdef shaders' lights to the game (none drawn in any timing capture), so no lowdef light reaches DI.
float3 DiLight(uint4 texel, float3 position, uint light) {
  return TwPointLight(DiSurfaceOf(texel, position), lights[light], light_colours[light].rgb, LightLinear(light), sun[uint2(1, 0)].xyz,
                      sun[uint2(SUN_LIGHT_TEXEL, 0)].w, 1.f, false);
}

// RTXDI's target for light `light` at a surface: its game light's luminance where its tile lists it (`members`), else 0
// (an unlisted light keeps the game's unshadowed light); NaN and negative 0.
float DiTarget(uint4 texel, float3 position, uint4 members, uint light) {
  if (light >= 128u || SetBit(members, light) == 0u) return 0.f;
  const float target = Luminance(DiLight(texel, position, light));
  return (target > 0.f) ? target : 0.f;
}

// The point of light `light`'s disk (light_size, under Ray Reconstruction; else its centre) a ray from `origin` goes to,
// `uv` in 0-1 each.
float3 DiSamplePoint(uint light, float3 origin, float2 uv) {
  const float3 centre = lights[light].xyz;
  if (light_size <= 0.f || (flags & RECONSTRUCTED) == 0u) return centre;
  return centre + DiskPoint(normalize(centre - origin), light_size, uv);
}

// Whether a ray from `origin` reaches `light_point`, stopping LIGHT_CLEARANCE short of it (rt_lights' rays).
bool DiVisible(float3 origin, float3 light_point) {
  const float3 to_light = light_point - origin;
  const float light_distance = length(to_light);
  if (!(light_distance > 1.0003098e-3f)) return false;
#if defined(DI_CONSERVATIVE)
  // Source: RTXDI RtxdiApplicationBridge.md, RAB_GetConservativeVisibility ("conservative visibility can skip
  // alpha-tested or translucent surfaces"; the initial samples' kept or discarded "computed in the same way").
  // Deviation (the NV1 variant, nvs_ nv_speedups.md N1; -D DI_CONSERVATIVE only in its rt_di_initial and reuse builds):
  // the solid surfaces alone (OccludedSolid's first query, without its alpha-tested CUT_OUTS one), so a leaf or cut-out
  // never blocks these rays: visibility only ever over-counted, the mean kept (RTXDI's rule), more noise where leaves
  // shadow torches. rt_di_shade's final ray and T1 stay exact (nvs_ C1): built without it.
  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = to_light / light_distance;
  ray.TMin = 0.f;
  ray.TMax = light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance);
  SCENE_QUERY(RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE) query;
  query.TraceRayInline(scene, RAY_FLAG_NONE, NO_GRASS_SMOKE, ray);
  query.Proceed();
  return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
#else
  return !Occluded(origin, to_light / light_distance, light_distance - min(LIGHT_CLEARANCE, 0.5f * light_distance), NO_GRASS_SMOKE);
#endif
}

// Light `light`'s place in the last trace's list (b4's light_previous; NO_LIGHT: new).
uint LightInLastList(uint light) {
  // Its byte (selected, not indexed).
  const uint4 words = light_previous[light >> 4u];
  const uint lane = (light >> 2u) & 3u;
  return (((lane == 0u) ? words.x : (lane == 1u) ? words.y : (lane == 2u) ? words.z : words.w) >> ((light & 3u) * 8u)) & 0xFFu;
}

// The last trace's lights' places in this one's list (the temporal pass's group builds it: DiCurrentClear, barrier,
// DiCurrentBuild, barrier); 0xFFFFFFFF: gone. Two lights at one place (equal position and reach) keep the first.
groupshared uint di_current_of[128];
void DiCurrentClear(uint index) {
  di_current_of[index] = 0xFFFFFFFFu;
  di_current_of[index + 64u] = 0xFFFFFFFFu;
}
void DiCurrentBuild(uint index) {
  const uint count = min(light_count, 128u);
  [unroll]
  for (uint pair = 0u; pair < 2u; ++pair) {
    const uint light = index + (pair * 64u);
    if (light >= count) continue;
    const uint previous = LightInLastList(light);
    if (previous != NO_LIGHT) InterlockedMin(di_current_of[previous], light);
  }
}
// The last trace's light `previous`'s index in this trace's list (0xFFFFFFFF: gone).
uint DiCurrentLight(uint previous) {
  return (previous < 128u) ? di_current_of[previous] : 0xFFFFFFFFu;
}

#endif  // SRC_GAMES_TWATTILA_DLAA_HELPER_RT_DI_SURFACE_HLSLI_
