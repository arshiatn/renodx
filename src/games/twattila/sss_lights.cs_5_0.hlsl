/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// Torch and fire shadows (screen_space_lights.hpp), one group: of the lights last frame's light draws had (t0 their
// instances as the draws gave them, t1 one record a draw: its first byte, count, stride, POSITION2's and POSITION1's
// offsets), the g_sss_lights_wanted best as the rays pick theirs (rt_core.hpp: their brightest channel x reach squared over
// their distance squared, at least 1 m; each once), then for each its Bend dispatches over the rectangle its reach covers
// on screen (sss_dispatch.hlsli), all in one list for the extended march's light mode (u0: [0] their count and groups,
// then two entries each; u1 the lights' coordinates; u2 the indirect arguments: up to 65535 groups a row), and the
// lights as rt_local_lights.hlsli's b9 holds them (u3: positions and reach, count, colours) for the light draws. Lights
// farther from the camera than g_sss_light_range (metres, 0 any) are not picked.
#include "./sss.hlsli"
#include "./sss_dispatch.hlsli"

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view_projection : packoffset(c9);
}

ByteAddressBuffer t_instances : register(t0);
StructuredBuffer<uint4> t_draws : register(t1);
RWStructuredBuffer<int4> t_rw_dispatches : register(u0);
RWStructuredBuffer<float4> t_rw_coordinates : register(u1);
RWByteAddressBuffer t_rw_arguments : register(u2);
RWStructuredBuffer<float4> t_rw_lights : register(u3);

static const uint THREADS = 256u;
static const uint MAX_CANDIDATES = 4096u;
static const uint MAX_LIGHTS = 32u;   // the mask's bits
static const uint LIST_LIGHTS = 128u;  // rt_local_lights.hlsli's b9 arrays
static const uint MAX_DRAWS = 256u;
static const int MAX_GROUPS_X = 65535;

static const uint NONE = 0xFFFFFFFFu;

groupshared float scores[MAX_CANDIDATES];
groupshared uint draw_starts[MAX_DRAWS + 1u];
groupshared uint picked;
groupshared uint taken[MAX_CANDIDATES / 32u];  // a bit a candidate: picked or left out as a repeat
groupshared float turn_scores[THREADS];
groupshared uint turn_candidates[THREADS];
groupshared uint repeated;
groupshared DispatchData light_dispatches[MAX_LIGHTS][8];
groupshared int light_dispatch_counts[MAX_LIGHTS];

// The order of the pick: the higher score first, of equal ones the earlier candidate.
bool PickedBefore(float score, uint candidate, float other_score, uint other)
{
  return score > other_score || (score == other_score && candidate < other);
}

// Candidate `index`'s instance: its first byte and the draw it is of.
uint InstanceByte(uint index, out uint4 draw)
{
  uint low = 0u;
  uint high = min(g_sss_light_draws, MAX_DRAWS) - 1u;
  [loop] while (low < high)
  {
    const uint middle = (low + high + 1u) >> 1;
    if (draw_starts[middle] <= index) {
      low = middle;
    } else {
      high = middle - 1u;
    }
  }
  draw = t_draws[low];
  return draw.x + (index - draw_starts[low]) * draw.z;
}

float4 LightPosition(uint index)
{
  uint4 draw;
  const uint at = InstanceByte(index, draw);
  return asfloat(t_instances.Load4(at + (draw.w & 0xFFFFu)));
}

[numthreads(THREADS, 1, 1)]
void main(uint thread : SV_GroupThreadID)
{
  const uint draws = min(g_sss_light_draws, MAX_DRAWS);
  if (thread == 0u)
  {
    uint start = 0u;
    for (uint draw = 0u; draw < draws; ++draw)
    {
      draw_starts[draw] = start;
      start += t_draws[draw].y;
    }
    draw_starts[draws] = start;
    picked = 0u;
  }
  GroupMemoryBarrierWithGroupSync();
  const uint candidates = (draws == 0u) ? 0u : min(draw_starts[draws], MAX_CANDIDATES);

  for (uint scored = thread; scored < candidates; scored += THREADS)
  {
    uint4 draw;
    const uint at = InstanceByte(scored, draw);
    const float4 light = asfloat(t_instances.Load4(at + (draw.w & 0xFFFFu)));
    const float3 colour = asfloat(t_instances.Load3(at + (draw.w >> 16)));
    const float3 offset = light.xyz - camera_position;
    const bool within = (g_sss_light_range <= 0.f) || (dot(offset, offset) <= g_sss_light_range * g_sss_light_range);
    scores[scored] = (light.w > 0.f && within) ? max(colour.r, max(colour.g, colour.b)) * light.w * light.w / max(dot(offset, offset), 1.f) : -1.f;
  }
  for (uint slot = thread; slot < MAX_CANDIDATES / 32u; slot += THREADS) taken[slot] = 0u;
  GroupMemoryBarrierWithGroupSync();

  // The g_sss_lights_wanted first in the pick's order, one a turn: each thread's first candidate not taken, then the
  // group's first of those. The same light drawn again (another of its draws: an earlier candidate with its score and
  // position) is taken without a place: only its first counts, and it comes first in the order. A rank for every
  // candidate against every other took 2.2 ms (8 lights) to 2.9 ms (32) of a battle's 5120 lights in 128 draws at 4K on
  // an RTX 5090; the lights, their order and the dispatches are the same. A position is read only where the scores
  // match: HLSL's && evaluates both sides (with the score's test it loaded both lights of every pair: 18.8 ms).
  const uint wanted = min(g_sss_lights_wanted, MAX_LIGHTS);
  [loop]
  for (uint turn = 0u; turn < candidates; ++turn)
  {
    float own_score = -1.f;
    uint own = NONE;
    for (uint candidate = thread; candidate < candidates; candidate += THREADS)
    {
      const float score = scores[candidate];
      // A NaN score (a NaN colour) is left out: the rank against every other gave it place 0 beside the first.
      if (!(score >= 0.f) || (taken[candidate >> 5] & (1u << (candidate & 31u))) != 0u) continue;
      if (own == NONE || PickedBefore(score, candidate, own_score, own))
      {
        own_score = score;
        own = candidate;
      }
    }
    turn_scores[thread] = own_score;
    turn_candidates[thread] = own;
    if (thread == 0u) repeated = 0u;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint half = THREADS / 2u; half > 0u; half >>= 1)
    {
      if (thread < half)
      {
        const uint other = turn_candidates[thread + half];
        const uint first = turn_candidates[thread];
        if (other != NONE && (first == NONE || PickedBefore(turn_scores[thread + half], other, turn_scores[thread], first)))
        {
          turn_scores[thread] = turn_scores[thread + half];
          turn_candidates[thread] = other;
        }
      }
      GroupMemoryBarrierWithGroupSync();
    }
    const uint best = turn_candidates[0];
    const float best_score = turn_scores[0];
    const uint done = picked;
    GroupMemoryBarrierWithGroupSync();
    if (best == NONE || done >= wanted) break;
    const float4 light = LightPosition(best);
    for (uint other = thread; other < best; other += THREADS)
    {
      if (scores[other] != best_score) continue;
      if (all(LightPosition(other) == light)) InterlockedOr(repeated, 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (thread == 0u)
    {
      taken[best >> 5] |= 1u << (best & 31u);
      if (repeated == 0u)
      {
        uint4 draw;
        const uint at = InstanceByte(best, draw);
        t_rw_lights[done] = asfloat(t_instances.Load4(at + (draw.w & 0xFFFFu)));
        t_rw_lights[LIST_LIGHTS + 1u + done] = float4(asfloat(t_instances.Load3(at + (draw.w >> 16))), 0.f);
        picked = done + 1u;
      }
    }
    GroupMemoryBarrierWithGroupSync();
  }

  // Each light's dispatches over the rectangle its reach covers (the screen where any corner of its box is behind the
  // camera).
  const uint count = picked;
  const int2 viewport_size = int2(g_sss_render_size);
  if (thread < count)
  {
    const float4 light = t_rw_lights[thread];
    float2 low = 1e30f;
    float2 high = -1e30f;
    bool whole = false;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner)
    {
      const float3 offset = float3((corner & 1u) ? light.w : -light.w, (corner & 2u) ? light.w : -light.w, (corner & 4u) ? light.w : -light.w);
      const float4 clip = mul(float4(light.xyz + offset, 1.f), view_projection);
      whole = whole || clip.w < 1e-3f;
      const float2 pixel = (clip.xy / max(clip.w, 1e-3f) * float2(0.5f, -0.5f) + 0.5f) * float2(viewport_size);
      low = min(low, pixel);
      high = max(high, pixel);
    }
    const int2 min_bounds = whole ? int2(0, 0) : clamp(int2(floor(low)) - 1, 0, viewport_size);
    const int2 max_bounds = whole ? viewport_size : clamp(int2(ceil(high)) + 1, 0, viewport_size);
    const float4 projected = mul(float4(light.xyz, 1.f), view_projection);
    const float4 coordinate = BendLightCoordinate(projected, projected.z / projected.w, viewport_size, WAVE_SIZE);
    t_rw_coordinates[thread] = coordinate;
    DispatchData dispatches[8];
    int dispatch_count = 0;
    if (all(max_bounds > min_bounds)) BuildDispatchList(coordinate, min_bounds, max_bounds, WAVE_SIZE, dispatches, dispatch_count);
    light_dispatches[thread] = dispatches;
    light_dispatch_counts[thread] = dispatch_count;
  }
  GroupMemoryBarrierWithGroupSync();

  // At most two screens' marching in all (a group a wave of 64 pixels): the brightest first, a light past it left lit
  // (near the camera each light's reach covers most of the screen, 0.9 ms at 4K on an RTX 5090).
  if (thread == 0u)
  {
    const int budget = 2 * viewport_size.x * viewport_size.y / WAVE_SIZE;
    int entries = 0;
    int groups = 0;
    for (uint light = 0u; light < count; ++light)
    {
      int light_groups = 0;
      for (int counted = 0; counted < light_dispatch_counts[light]; ++counted)
      {
        const int3 waves = light_dispatches[light][counted].wave_count;
        light_groups += waves.x * waves.y * waves.z;
      }
      if (groups + light_groups > budget) continue;
      for (int dispatch = 0; dispatch < light_dispatch_counts[light]; ++dispatch)
      {
        const DispatchData data = light_dispatches[light][dispatch];
        t_rw_dispatches[1 + 2 * entries] = int4(groups, data.wave_count.y, data.wave_count.z, (int)light);
        t_rw_dispatches[2 + 2 * entries] = int4(data.wave_offset * WAVE_SIZE, 0, 0);
        groups += data.wave_count.x * data.wave_count.y * data.wave_count.z;
        ++entries;
      }
    }
    t_rw_dispatches[0] = int4(entries, groups, 0, 0);
    const uint rows = (uint)(groups + MAX_GROUPS_X - 1) / (uint)MAX_GROUPS_X;
    t_rw_arguments.Store3(0, uint3((uint)min(groups, MAX_GROUPS_X), max(rows, 1u), (groups > 0) ? 1u : 0u));
    t_rw_lights[LIST_LIGHTS] = float4(asfloat(count), 0.f, 0.f, 0.f);
  }
}
