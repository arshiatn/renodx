// Path tracing at Bandai's lighting pass (rt_shadows.hpp TraceLighting; 0x7A1E0D1C: a DXR 1.2 ray generation shader
// with shader execution reordering and opacity micromaps; 0x7A1E0D1B: the same as a compute shader for older GPUs and
// drivers), from the G-buffer (normal, metallic / specular / roughness, base colour) and the sun shadow mask's
// positions:
// - Reflections (and specular occlusion): GGX samples of each pixel's own lobe (one mirror ray for smooth surfaces), the
//   light along each (PathRadiance) where it meets a surface, its share of samples (the game keeps its own reflection
//   where a ray leaves the scene: the sky), and the share that meets nothing within the reflection range (specular
//   occlusion, for the game's cubemap reflection).
// - Path-traced bounce light (Path tracing): cosine-weighted paths of up to path_bounces surfaces past the first.
// Bandai's lighting is hand-tuned, so the paths do not shade with a model of their own: a surface last frame saw (or the
// radiance cache knows) gives its scene colour and ends the path; one nobody saw gets the sun through a shadow ray and
// the shade light the game gives (rt_cache.hlsli's learned lighting) times its base colour (the cache's), and the path
// goes on from it; what such a surface sends on feeds the cache. A ray from the camera's pixel that leaves the scene adds
// nothing (the game's own sky light covers it); from an unseen surface the learned shade light stands for the sky.
// Accumulated over frames here (by world position), filtered after (0x7A1E0D14 the bounce light, 0x7A1E0D1E the
// reflections). World units: UE's centimetres.
#ifndef SRC_GAMES_TALESOFARISE_DLSS_RT_PATH_HLSLI_
#define SRC_GAMES_TALESOFARISE_DLSS_RT_PATH_HLSLI_

#include "./rt_cache.hlsli"
#include "./rt_scene.hlsli"

Texture2D<float4> normals : register(t6);     // G-buffer A: world normal x 0.5 + 0.5
Texture2D<float4> material : register(t13);   // G-buffer B: metallic, specular, roughness, shading model
Texture2D<float4> base_color : register(t14); // G-buffer C
RWTexture2D<float4> accumulated : register(u2);           // bounce light: rgb, frames
RWTexture2D<float4> history : register(u3);               // last frame's (0x7A1E0D14 writes it)
RWTexture2D<float4> specular_accumulated : register(u16);  // reflection: rgb, the share of samples that met a surface
RWTexture2D<float4> specular_history : register(u17);      // last frame's (0x7A1E0D1E writes it)
RWTexture2D<float2> specular_meta : register(u23);         // specular occlusion, frames
RWTexture2D<float2> specular_meta_history : register(u24);

static const float MAX_DIFFUSE_FRAMES = 12.f;
static const float MAX_SPECULAR_FRAMES = 12.f;
// Rougher surfaces keep the game's reflection (its cubemap): only their specular occlusion is traced.
static const float ROUGH_LIMIT = 0.75f;
static const float UNSEEN_ALBEDO = 0.35f;  // a surface's base colour the cache never saw

#if defined(PATH_REORDER)
// Shader execution reordering (DXR 1.2): threads regroup by what their first hit is before shading it (the screen
// lookup, the cache, a shadow ray and the path's next rays diverge most there).
#define REORDER(query, hint)                                               \
  {                                                                        \
    dx::HitObject reorder_hit = dx::HitObject::FromRayQuery(query);        \
    dx::MaybeReorderThread(reorder_hit, (hint), 4);                        \
  }
#else
#define REORDER(query, hint)
#endif

float3 Camera() {
  return sun[uint2(1, 0)].xyz;
}

// The base colour the cache saw at a hit.
float3 HitAlbedo(float3 hit) {
  float3 albedo;
  return CacheAlbedo(hit, Camera(), albedo) ? albedo : UNSEEN_ALBEDO;
}

// The light arriving along a ray from a surface the camera sees. `first_distance`: how far its first surface is
// (1e30: none).
float3 PathRadiance(float3 origin, float3 direction, float range, uint2 pixel, uint path, out float first_distance) {
  first_distance = 1e30f;
  float3 radiance = 0.f;
  float3 throughput = 1.f;
  const float3 camera = Camera();
  const float4 toward_sun = sun[uint2(0, 0)];
  const float4 shade = light_kept[0];
  const float3 sunlit = light_kept[1].rgb;
  bool unseen = false;
  float3 unseen_position = 0.f;
  float3 unseen_throughput = 1.f;
  float3 radiance_before = 0.f;
  [loop]
  for (uint vertex = 0u; vertex <= path_bounces; ++vertex) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.f;
    ray.TMax = (vertex == 0u) ? range : bounce_range;
    TRACE_NEAREST(query, ray, EVERYTHING)
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    if (vertex == 0u) {
      REORDER(query, hit ? 1u + (query.CommittedInstanceID() & 7u) : 0u)
    }
    if (!hit) {
      if (vertex > 0u) radiance += throughput * shade.rgb;
      break;
    }
    const float3 position = origin + direction * query.CommittedRayT();
    if (vertex == 0u) first_distance = query.CommittedRayT();
    float3 light;
    if (HitLight(position, light)) {
      radiance += throughput * light;
      break;
    }
    // A surface nobody saw: the sun through a shadow ray, and on.
    const float3 normal = HitNormal(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(), direction);
    const float3 albedo = HitAlbedo(position);
    const float3 surface = position + normal * SurfaceOffset(distance(position, camera));
    float3 emitted = 0.f;
    const float n_dot_l = dot(normal, toward_sun.xyz);
    if (toward_sun.w != 0.f && n_dot_l > 0.f && !Occluded(surface, toward_sun.xyz, 1.0e6f, EVERYTHING)) {
      emitted = albedo * sunlit * n_dot_l;
    }
    if (!unseen) {
      unseen = true;
      unseen_position = position;
      unseen_throughput = throughput;
      radiance_before = radiance;
    }
    if (vertex == path_bounces) {
      radiance += throughput * (emitted + albedo * shade.rgb);
      break;
    }
    radiance += throughput * emitted;
    throughput *= albedo;
    origin = surface;
    direction = CosineDirection(normal, Random(uint3(pixel, frame * 64u + path * 8u + vertex * 2u + 8192u)),
                                Random(uint3(pixel, frame * 64u + path * 8u + vertex * 2u + 8193u)));
  }
  // The first unseen surface's light (what the rest of the path found from it) feeds the cache, once the learned
  // lighting has been solved.
  if (unseen && shade.w > 0.f && all(unseen_throughput > 1e-3f)) {
    CacheAdd(unseen_position, camera, (radiance - radiance_before) / unseen_throughput);
  }
  return radiance;
}

void PathTracePixel(uint2 id) {
  uint2 size;
  positions.GetDimensions(size.x, size.y);
  if (any(id >= size)) return;
  const bool path_tracing = (flags & PATH_TRACING) != 0u;
  const bool reflections_on = (flags & REFLECTIONS) != 0u;
  const bool occlusion = (flags & SPECULAR_OCCLUSION) != 0u;
  const float4 position = positions[id];
  const float3 encoded = normals[id].xyz;
  if (position.w <= 0.f || dot(encoded, encoded) == 0.f) {
    if (path_tracing) accumulated[id] = 0.f;
    specular_accumulated[id] = 0.f;
    specular_meta[id] = float2(1.f, 0.f);
    return;
  }
  const float3 normal = normalize(encoded * 2.f - 1.f);
  const float3 view = normalize(Camera() - position.xyz);
  const float n_dot_v = max(dot(normal, view), 1e-3f);
  const float3 origin = position.xyz + normal * SurfaceOffset(position.w);
  int2 previous_pixel;
  const bool seen = (flags & HISTORY_VALID) != 0u && PreviousPixel(position.xyz, previous_pixel)
                    && SeenLastFrame(position.xyz, previous_pixel);

  if (path_tracing) {
    float3 light = 0.f;
    [loop]
    for (uint path = 0u; path < path_samples; ++path) {
      const float3 direction = CosineDirection(normal, Random(uint3(id, frame * 16u + path * 2u + 1024u)),
                                               Random(uint3(id, frame * 16u + path * 2u + 1025u)));
      float first_distance;
      light += PathRadiance(origin, direction, bounce_range, id, path, first_distance);
    }
    light /= max(path_samples, 1u);
    const float4 previous = ((flags & BOUNCE_HISTORY) != 0u && seen) ? history[previous_pixel] : 0.f;
    const float frames = min(previous.w + 1.f, MAX_DIFFUSE_FRAMES);
    accumulated[id] = float4(lerp(previous.rgb, light, 1.f / frames), frames);
  }

  if (!reflections_on && !occlusion) return;
  const float roughness = saturate(material[id].b);
  const float alpha = max(roughness * roughness, 1e-3f);
  const bool mirror = roughness < 0.05f;
  const bool traced = reflections_on && roughness <= ROUGH_LIMIT;
  const uint samples = mirror ? 1u : max(path_samples, 1u);
  const float3x3 basis = Basis(normal);
  const float3 view_local = float3(dot(view, basis[0]), dot(view, basis[1]), n_dot_v);
  float3 light = 0.f;
  float weights = 0.f;
  uint met = 0u;
  uint open = 0u;
  [loop]
  for (uint sample = 0u; sample < samples; ++sample) {
    const float3 half_local = mirror ? float3(0.f, 0.f, 1.f)
                                     : GgxVisibleNormal(view_local, alpha, Random(uint3(id, frame * 16u + sample * 2u + 3072u)),
                                                        Random(uint3(id, frame * 16u + sample * 2u + 3073u)));
    const float3 half_vector = half_local.x * basis[0] + half_local.y * basis[1] + half_local.z * normal;
    const float3 direction = reflect(-view, half_vector);
    const float n_dot_l = dot(normal, direction);
    if (n_dot_l <= 0.f) continue;
    if (traced) {
      float first_distance;
      const float3 sample_light = PathRadiance(origin, direction, 1.0e6f, id, sample + 4u, first_distance);
      if (first_distance < 1e29f) {
        const float weight = mirror ? 1.f : GgxSampleWeight(n_dot_v, n_dot_l, alpha);
        light += sample_light * weight;
        weights += weight;
        ++met;
      }
      if (first_distance > reflection_range) ++open;
    } else if (!Occluded(origin, direction, reflection_range, EVERYTHING)) {
      ++open;
    }
  }
  const float4 current = float4((weights > 0.f) ? light / weights : 0.f, (float)met / samples);
  const float current_open = (float)open / samples;
  // Mirrors keep no history (one exact ray); rougher lobes up to MAX_SPECULAR_FRAMES.
  float4 previous = 0.f;
  float2 previous_meta = float2(1.f, 0.f);
  if ((flags & SPECULAR_HISTORY) != 0u && seen && !mirror) {
    previous = specular_history[previous_pixel];
    previous_meta = specular_meta_history[previous_pixel];
  }
  const float frames = min(previous_meta.y + 1.f, mirror ? 1.f : lerp(2.f, MAX_SPECULAR_FRAMES, saturate(roughness * 3.f)));
  specular_accumulated[id] = lerp(previous, current, 1.f / frames);
  specular_meta[id] = float2(lerp(previous_meta.x, current_open, 1.f / frames), frames);
}

#endif  // SRC_GAMES_TALESOFARISE_DLSS_RT_PATH_HLSLI_
