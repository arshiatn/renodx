/*
 * Copyright (C) 2026 ArshiaTN
 * SPDX-License-Identifier: MIT
 */

// The reference build's passes (rt_reference.hlsli; tests only: build.ps1 with RENODX_TW_REFERENCE_BUILD compiles one
// header per -D REF_* into rt_reference_*.h, rt_d3d12.hpp runs them under RENODX_TW_REFERENCE):
// - REF_ACCUMULATE: the candidate's output at its grid into its batch means and their Welford sums (B1);
// - REF_CENSUS: one output's NaN, Inf, negative and above-FP16 counts (a run with any is INVALID);
// - REF_DIGEST: a raw buffer range (t8) hashed into this frame's scene digest (an order-free sum of its elements' hashes);
// - REF_STATE: one thread's bookkeeping (REF_OP_*);
// - REF_PATHS: a slice of the paths' reference at the bounce texels (rt_reference.hlsli's variants);
// - REF_REFLECT: a slice of the reflections' share and specular occlusion at the render pixels (sums of the lobe's weights);
// - REF_GROUP: a group's mean into the Welford sums over groups (and its own picture).
#include "./rt_reference.hlsli"

#if defined(REF_CENSUS)
RWTexture2D<float4> ref_fog_lights_output : register(u34);  // rt_fog.cs_6_5.hlsl's

// A value's four checks: NaN, Inf, below 0 (`signed_ok`: not counted), above FP16's most.
void Count(uint output, uint2 id, float4 value, bool4 signed_ok) {
  const bool4 not_a_number = isnan(value);
  const bool4 infinite = isinf(value);
  const bool4 negative = and(and(!not_a_number, value < 0.f), !signed_ok);
  const bool4 overflowing = and(and(!not_a_number, !infinite), value > 65504.f);
  const bool4 found[4] = {not_a_number, infinite, negative, overflowing};
  bool bad = false;
  [unroll]
  for (uint kind = 0u; kind < 4u; ++kind) {
    if (!any(found[kind])) continue;
    InterlockedAdd(ref_state[REF_STATE_CENSUS + output * 4u + kind], 1u);
    bad = true;
  }
  uint previous;
  if (bad) InterlockedCompareExchange(ref_state[REF_STATE_FIRST_BAD + output], 0u, (id.x | (id.y << 16)) + 1u, previous);
}
#endif

#if defined(REF_DIGEST)
[numthreads(64, 1, 1)]
#else
[numthreads(8, 8, 1)]
#endif
void main(uint3 id : SV_DispatchThreadID) {
#if defined(REF_ACCUMULATE)
  if (id.x >= ref_f || id.y >= ref_g) return;
  float4 value;
  switch (ref_word) {
    case REF_SOURCE_BOUNCE:
      value = bounce[id.xy];
      break;
    case REF_SOURCE_REFLECTION:
      value = reflection[id.xy];
      break;
    case REF_SOURCE_SPECULAR_OCCLUSION:
      value = float4(specular_occlusion[id.xy], 0.f, 0.f, 0.f);
      break;
    case REF_SOURCE_VISIBILITY:
      value = float4(traced_visibility[id.xy], 0.f, 0.f, 0.f);
      break;
    default:
      value = float4(ref_ambient_output[id.xy], 0.f, 0.f, 0.f);
      break;
  }
  // A colour's w its luminance (the compare's luminance variance: the channels' covariance kept).
  if (ref_word <= REF_SOURCE_REFLECTION) value.w = dot(value.rgb, float3(0.2126f, 0.7152f, 0.0722f));
  ref_candidate_last[id.xy] = value;
  const float4 batch = ((ref_a == 0u) ? 0.f : ref_candidate_batch[id.xy]) + value;
  ref_candidate_batch[id.xy] = batch;
  if (ref_a + 1u < REF_BATCH) return;
  const float4 batch_mean = batch / (float)REF_BATCH;
  if (ref_b == 0u) {
    ref_candidate_shift[id.xy] = batch_mean;
    ref_candidate_mean[id.xy] = 0.f;
    ref_candidate_m2[id.xy] = 0.f;
    ref_candidate_cross[id.xy] = 0.f;
    ref_candidate_previous[id.xy] = 0.f;
    return;
  }
  const float4 shifted = batch_mean - ref_candidate_shift[id.xy];
  const float4 mean = ref_candidate_mean[id.xy];
  const float4 delta = shifted - mean;
  const float4 new_mean = mean + delta / (float)(ref_b + 1u);
  ref_candidate_mean[id.xy] = new_mean;
  ref_candidate_m2[id.xy] = ref_candidate_m2[id.xy] + delta * (shifted - new_mean);
  ref_candidate_cross[id.xy] = ref_candidate_cross[id.xy] + shifted * ref_candidate_previous[id.xy];
  ref_candidate_previous[id.xy] = shifted;
#elif defined(REF_CENSUS)
  if (id.x >= ref_f || id.y >= ref_g) return;
  // World positions in a reservoir and the candidate's shifted sums may be below 0; their light, W and the M2 may not.
  switch (ref_word) {
    case REF_OUT_BOUNCE:
      Count(ref_word, id.xy, bounce[id.xy], false);
      break;
    case REF_OUT_REFLECTION:
      Count(ref_word, id.xy, reflection[id.xy], false);
      break;
    case REF_OUT_SPECULAR:
      Count(ref_word, id.xy, specular_accumulated[id.xy], false);
      break;
    case REF_OUT_OWN_SKY:
    case REF_OUT_OWN_SKY + 1u:
      Count(ref_word, id.xy, own_sky[ref_word - REF_OUT_OWN_SKY][id.xy], false);
      break;
    case REF_OUT_REUSE:
    case REF_OUT_REUSE + 1u: {
      const uint set = ref_word - REF_OUT_REUSE;
      const uint4 packed = reuse_data[set][id.xy];
      Count(ref_word, id.xy, float4(f16tof32(packed.x), f16tof32(packed.x >> 16), f16tof32(packed.y), reuse_samples[set][id.xy].w), false);
      break;
    }
    case REF_OUT_FOG_LIGHTS:
      Count(ref_word, id.xy, ref_fog_lights_output[id.xy], false);
      break;
    case REF_OUT_REF_MEAN:
      Count(ref_word, id.xy, ref_mean[id.xy], false);
      break;
    case REF_OUT_REF_M2:
      Count(ref_word, id.xy, ref_m2[id.xy], false);
      break;
    case REF_OUT_CANDIDATE_M2:
      Count(ref_word, id.xy, ref_candidate_m2[id.xy], false);
      break;
    default:
      Count(ref_word, id.xy, ref_candidate_mean[id.xy], true);
      break;
  }
#elif defined(REF_DIGEST)
  // A 64-byte element a thread (an instance desc: its transform, ID, mask, flags; its BLAS's address left out, which is
  // another in every process; a record: four of them), hashed with the lane and summed without its place: the same
  // scene in another order is the same digest (placed instances come in the order the GPU wrote them).
  if (id.x >= ref_a) return;
  uint4 words[4] = {ref_digest_input.Load4(id.x * 64u), ref_digest_input.Load4(id.x * 64u + 16u), ref_digest_input.Load4(id.x * 64u + 32u),
                    ref_digest_input.Load4(id.x * 64u + 48u)};
  if (ref_word == 0u) words[3].zw = 0u;
  uint4 hash = Pcg4d(words[0] ^ uint4(ref_word, 0x85EBCA6Bu, 0xC2B2AE35u, 0x27D4EB2Fu));
  hash = Pcg4d(hash ^ words[1]);
  hash = Pcg4d(hash ^ words[2]);
  hash = Pcg4d(hash ^ words[3]);
  // Records keep their place: InstanceID + geometry index names one.
  if (ref_word != 0u) hash = Pcg4d(hash ^ id.x);
  InterlockedAdd(ref_state[REF_STATE_DIGEST], hash.x);
  InterlockedXor(ref_state[REF_STATE_DIGEST + 1u], hash.y);
  InterlockedAdd(ref_state[REF_STATE_DIGEST + 2u], hash.z);
  InterlockedXor(ref_state[REF_STATE_DIGEST + 3u], hash.w);
#elif defined(REF_STATE)
  if (any(id.xy != 0u)) return;
  switch (ref_word) {
    case REF_OP_CLEAR:
      for (uint word = ref_a; word < ref_a + ref_b; ++word) ref_state[word] = 0u;
      break;
    case REF_OP_DIGEST_BEGIN:
      for (uint lane = 0u; lane < 4u; ++lane) ref_state[REF_STATE_DIGEST + lane] = 0u;
      break;
    case REF_OP_DIGEST_END: {
      ref_state[REF_STATE_DIGEST] += ref_a;
      ref_state[REF_STATE_DIGEST + 1u] ^= ref_b;
      const uint4 now = uint4(ref_state[REF_STATE_DIGEST], ref_state[REF_STATE_DIGEST + 1u], ref_state[REF_STATE_DIGEST + 2u], ref_state[REF_STATE_DIGEST + 3u]);
      const uint4 last = uint4(ref_state[REF_STATE_DIGEST_LAST], ref_state[REF_STATE_DIGEST_LAST + 1u], ref_state[REF_STATE_DIGEST_LAST + 2u],
                               ref_state[REF_STATE_DIGEST_LAST + 3u]);
      const uint4 frozen = uint4(ref_state[REF_STATE_DIGEST_FROZEN], ref_state[REF_STATE_DIGEST_FROZEN + 1u], ref_state[REF_STATE_DIGEST_FROZEN + 2u],
                                 ref_state[REF_STATE_DIGEST_FROZEN + 3u]);
      const uint digest_flags = ref_state[REF_STATE_DIGEST_FLAGS];
      if ((digest_flags & 2u) != 0u && any(now != last)) ref_state[REF_STATE_DIGEST_CHANGES] += 1u;
      ref_state[REF_STATE_MISMATCH] = ((digest_flags & 1u) != 0u && any(now != frozen)) ? 1u : 0u;
      for (uint lane = 0u; lane < 4u; ++lane) ref_state[REF_STATE_DIGEST_LAST + lane] = now[lane];
      ref_state[REF_STATE_DIGEST_FLAGS] = digest_flags | 2u;
      break;
    }
    case REF_OP_DIGEST_FREEZE:
      for (uint lane = 0u; lane < 4u; ++lane) ref_state[REF_STATE_DIGEST_FROZEN + lane] = ref_state[REF_STATE_DIGEST + lane];
      ref_state[REF_STATE_DIGEST_FLAGS] |= 1u;
      ref_state[REF_STATE_MISMATCH] = 0u;
      break;
    default:
      if (ref_state[REF_STATE_GROUP_SLICES + ref_a] != 0u) ref_state[REF_STATE_GROUPS_DONE] += 1u;
      break;
  }
#elif defined(REF_GROUP)
  if (id.x >= ref_f || id.y >= ref_g) return;
  const uint slices = ref_state[REF_STATE_GROUP_SLICES + ref_word];
  const float4 group_sum = ref_group_sum[id.xy];
  ref_group_sum[id.xy] = 0.f;
  if (slices == 0u) return;
  // The sum's w counts the samples taken (sky texels none: their group mean 0).
  const float4 group_mean = (group_sum.w > 0.f) ? group_sum / group_sum.w : 0.f;
  if (ref_c != 0u) ref_groups[uint3(id.xy, ref_word)] = group_mean;
  const uint finished = ref_state[REF_STATE_GROUPS_DONE];
  if (finished == 0u) {
    ref_mean[id.xy] = group_mean;
    ref_m2[id.xy] = 0.f;
    return;
  }
  const float4 mean = ref_mean[id.xy];
  const float4 delta = group_mean - mean;
  const float4 new_mean = mean + delta / (float)(finished + 1u);
  ref_mean[id.xy] = new_mean;
  ref_m2[id.xy] = ref_m2[id.xy] + delta * (group_mean - new_mean);
#else
  // A slice: dropped where this frame's scene digest is not the freeze frame's (counted once, by texel 0).
  const bool counter = all(id.xy == 0u);
  if (ref_state[REF_STATE_MISMATCH] != 0u) {
    if (counter) InterlockedAdd(ref_state[REF_STATE_DROPPED], 1u);
    return;
  }
  if (counter) InterlockedAdd(ref_state[REF_STATE_GROUP_SLICES + ref_c], 1u);
  if (id.x >= ref_f || id.y >= ref_g) return;
  const uint variant = ref_word & 0xFFu;
  uint overflow = 0u;
  uint cut = 0u;
  uint met = 0u;
  uint above_count = 0u;
  float4 sum = 0.f;
#if defined(REF_PATHS)
  const uint2 pixel = min(id.xy * bounce_scale + bounce_scale / 2u, uint2(width, height) - 1u);
#else
  const uint2 pixel = id.xy;
#endif
  const float4 position = positions[pixel];
  if (position.w > 0.f) {
    float3 normal;
    float3 origin;
    float3 surface = position.xyz;
#if defined(REF_PATHS)
    if (variant == REF_SAME_START) {
      TexelSurface(id.xy, normal, origin);
      met = 1u;
    } else {
      met = RefPrimary(pixel, position, surface, normal, overflow) ? 1u : 0u;
      origin = RefOffset(surface, normal);
    }
#else
    if (variant == REF_SAME_START) {
      normal = PixelNormal(pixel, position);
      origin = RayOrigin(position, normal, Planar(int2(pixel), position, normal));
      met = 1u;
    } else {
      met = RefPrimary(pixel, position, surface, normal, overflow) ? 1u : 0u;
      origin = RefOffset(surface, normal);
    }
#endif
#if defined(REF_PATHS)
    const bool furnace = variant == REF_FURNACE;
    // The furnace's box closed for every ray (no first-ray reach: G1).
    const float first_range = furnace ? 1.0e4f : (((path_flags & PATH_AMBIENT) != 0u) ? min(bounce_range, sky_range) : bounce_range);
    const float3 screen_albedo = ((path_flags & PATH_GUIDES) != 0u) ? guide_albedo[pixel].rgb : UNSEEN_ALBEDO;
    const float3 direct = ((ref_word & REF_DIRECT) != 0u) ? RefLights(origin, normal, 32u, overflow) : 0.f;
#else
    const float roughness = saturate(guide_roughness[pixel]);
    const float alpha = ReflectAlpha(roughness);
    const bool mirror = roughness < MIRROR_ROUGHNESS;
    // Traced as the helper traces it (rt_path.hlsli ReflectPixel): with reflections up to ROUGH_LIMIT; occlusion alone, every roughness.
    const bool traced = (path_flags & PATH_REFLECTIONS) != 0u && roughness <= ROUGH_LIMIT;
    float3 tangent;
    float3 bitangent;
    RefBasis(normal, tangent, bitangent);
    const float3 view = normalize(Camera() - surface);
    const float3 view_local = float3(dot(view, tangent), dot(view, bitangent), dot(view, normal));
#endif
    [loop]
    for (uint sample = 0u; sample < ref_a; ++sample) {
      const uint in_group = ref_b + sample;
      const uint sample_index = ref_c * ref_d + in_group;
      const float2 u = RefStratified(in_group, ref_d, float2(RefRandom(id.xy, sample_index, 0u), RefRandom(id.xy, sample_index, 1u)));
#if defined(REF_PATHS)
      if (variant == REF_LOUT) {
        // The light the screen's surface sends back under the screen route's cut: its base colour x the sun's (a point of
        // its disk) and every local light's, no bounce.
        float3 leaving = RefSunLight() * max(dot(normal, sun[uint2(0, 0)].xyz), 0.f) * RefSunVisibility(origin, normal, u, overflow);
        if ((path_flags & PATH_LOCAL_LIGHTS) != 0u) leaving += RefLights(origin, normal, 128u, overflow);
        sum += float4(screen_albedo * leaving, 1.f);
      } else if (variant == REF_SAME_ROUTE) {
        sum += float4(direct + RefRouteIncoming(origin, RefCosineDirection(normal, u), normal, first_range, id.xy, sample_index, overflow), 1.f);
      } else {
        sum += float4(direct + RefIncoming(origin, RefCosineDirection(normal, u), normal, first_range, furnace, variant == REF_CONTACT || variant == REF_CONTACT_SKY,
                                           variant == REF_CONTACT_SKY, id.xy, sample_index, overflow, cut),
                      1.f);
      }
#else
      // The lobe's weight w = D G2 / (4 n.v) over Heitz's visible-normal density G1 D / (4 n.v): G2 / G1 (0 below the
      // horizon). Sums: x the weight where the lobe meets a surface (traced) or is open within reflection_range (occlusion),
      // y the weight, z the same unweighted (the estimator's own share), w the samples; the aux's w the samples above the
      // horizon.
      float3 direction;
      float weight;
      if (mirror) {
        direction = reflect(-view, normal);
        weight = 1.f;
      } else {
        const float3 half_local = RefVisibleNormal(view_local, alpha, u);
        const float3 light_local = 2.f * dot(view_local, half_local) * half_local - view_local;
        direction = normalize(tangent * light_local.x + bitangent * light_local.y + normal * light_local.z);
        weight = RefVisibleNormalWeight(view_local.z, light_local.z, alpha);
      }
      const bool above = dot(direction, normal) > 0.f;
      bool counted = false;
      if (above && traced) {
        float hit_distance;
        uint record_index;
        float3 hit_normal;
        counted = RefNearest(origin, direction, 1.0e4f, NO_GRASS_SMOKE, hit_distance, record_index, hit_normal, overflow);
      } else if (above) {
        counted = !RefBlocked(origin, direction, reflection_range, NO_GRASS_SMOKE, overflow);
      }
      sum += float4(counted ? weight : 0.f, weight, counted ? 1.f : 0.f, 1.f);
      above_count += above ? 1u : 0u;
#endif
    }
  }
  // The synthetic control (REF_SCALED): the reference x ref_parameters[1].x.
  if ((ref_word & REF_SCALED) != 0u) sum.xyz *= ref_parameters[1].x;
  ref_group_sum[id.xy] = ref_group_sum[id.xy] + sum;
  ref_aux[id.xy] = ref_aux[id.xy] + uint4(met * ref_a, overflow, cut, above_count);
#endif
}
