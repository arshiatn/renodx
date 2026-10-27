// Ray-traced sun shadows, ambient occlusion and bounce light (D3D12 inline ray queries; 0x7A1E0D33 is this with
// opacity micromaps, shader model 6.9) against the captured scene (static meshes, alpha-tested ones, characters as they
// are this frame), per pixel from its world position: the sun's visibility (a ray toward the sun: 0 blocked, 1 lit; 1
// for the sky, surfaces facing away and frames without a sun; for soft shadows also the distance to what blocks it,
// which 0x7A1E0D1F turns into penumbras), the ambient visibility (cosine-weighted rays around the normal, as far as the range: the share that escapes)
// and the bounce light (cosine-weighted rays to the nearest surface: last frame's scene colour where last frame saw it,
// else the radiance cache's), accumulated over frames (0x7A1E0D14 filters it). With path tracing on, the bounce light
// comes from the lighting pass (0x7A1E0D1B / 0x7A1E0D1C) instead.
// Characters cast sun shadows on the world, not on themselves (the game's shadow maps shade them; rays on their folds
// and faces streak): a ray from the camera finds whether a pixel is a character's.
// Noise moves every frame, which the accumulation, DLSS and Ray Reconstruction settle.
#include "./rt_cache.hlsli"
#include "./rt_scene.hlsli"

RWTexture2D<unorm float> visibility : register(u0);
RWTexture2D<unorm float> ambient : register(u1);
RWTexture2D<float4> accumulated : register(u2);  // bounce light: rgb, frames accumulated
RWTexture2D<float4> history : register(u3);      // last frame's accumulation (0x7A1E0D14 writes it)
RWTexture2D<float2> sun_rays : register(u25);  // soft shadows: lit, blocker distance (0x7A1E0D1F filters them)

// The bounce light keeps at most this many frames (about a fifth of a second at 60 fps: lights and the sun move).
static const float MAX_FRAMES = 12.f;

// Whether the pixel at `position` (`offset` its surface offset) is a character's: the camera's ray reaches a
// character there.
bool OnCharacter(float4 position, float offset) {
  const float3 camera = sun[uint2(1, 0)].xyz;
  const float3 to_pixel = position.xyz - camera;
  const float pixel_distance = length(to_pixel);
  RayDesc ray;
  ray.Origin = camera;
  ray.Direction = to_pixel / pixel_distance;
  ray.TMin = 0.f;
  ray.TMax = pixel_distance + 4.f * offset;
  TRACE_NEAREST(query, ray, CHARACTERS)
  return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && query.CommittedRayT() > pixel_distance - 4.f * offset;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  visibility.GetDimensions(size.x, size.y);
  if (any(id.xy >= size)) return;
  const float4 position = positions[id.xy];
  const float4 toward_sun = sun[uint2(0, 0)];
  const bool bounce = (flags & BOUNCE_LIGHT) != 0u && (flags & PATH_TRACING) == 0u;
  if (position.w <= 0.f) {
    visibility[id.xy] = 1.f;
    ambient[id.xy] = 1.f;
    if (bounce) accumulated[id.xy] = 0.f;
    if (sun_cone > 0.f) sun_rays[id.xy] = float2(1.f, 0.f);
    return;
  }
  const float3 normal = SurfaceNormal(int2(id.xy), int2(size), position);
  const float offset = SurfaceOffset(position.w);
  const float3 origin = position.xyz + normal * offset;

  float sun_visibility = 1.f;
  float blocker_distance = 0.f;
  // Facing away from the sun: the game's lighting is dark there already.
  if ((flags & SUN_SHADOWS) != 0u && toward_sun.w != 0.f && dot(normal, toward_sun.xyz) > 0.f) {
    const uint mask = ((flags & CHARACTER_SHADOWS) != 0u && !OnCharacter(position, offset)) ? EVERYTHING : WORLD;
    const float3 sun_origin = position.xyz + (normal + toward_sun.xyz) * offset;
    if (sun_cone > 0.f) {
      // Soft shadows need the nearest blocker's distance.
      RayDesc ray;
      ray.Origin = sun_origin;
      ray.Direction = toward_sun.xyz;
      ray.TMin = 0.f;
      ray.TMax = 1.0e6f;
      TRACE_NEAREST(query, ray, mask)
      if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        sun_visibility = 0.f;
        blocker_distance = query.CommittedRayT();
      }
    } else {
      sun_visibility = Occluded(sun_origin, toward_sun.xyz, 1.0e6f, mask) ? 0.f : 1.f;
    }
  }
  if (sun_cone > 0.f) {
    sun_rays[id.xy] = float2(sun_visibility, blocker_distance);
  } else {
    visibility[id.xy] = sun_visibility;
  }

  float ambient_visibility = 1.f;
  if ((flags & AMBIENT_OCCLUSION) != 0u && ambient_rays != 0u) {
    uint escaped = 0u;
    [loop]
    for (uint ray = 0; ray < ambient_rays; ++ray) {
      const float3 direction = CosineDirection(normal, Random(uint3(id.xy, frame * 16u + ray * 2u)),
                                               Random(uint3(id.xy, frame * 16u + ray * 2u + 1u)));
      if (!Occluded(origin, direction, ambient_range, EVERYTHING)) ++escaped;
    }
    ambient_visibility = (float)escaped / ambient_rays;
  }
  ambient[id.xy] = ambient_visibility;

  if (!bounce) return;
  // Bounce light: the light leaving the nearest surface along each ray (Lambert: the base colour times the mean of
  // what cosine-weighted rays see): last frame's scene colour, or the radiance cache's for what last frame did not see
  // (off screen, behind something; nothing where the cache has not seen it either).
  float3 light = 0.f;
  [loop]
  for (uint ray = 0; ray < bounce_rays; ++ray) {
    RayDesc desc;
    desc.Origin = origin;
    desc.Direction = CosineDirection(normal, Random(uint3(id.xy, frame * 16u + ray * 2u + 1024u)),
                                     Random(uint3(id.xy, frame * 16u + ray * 2u + 1025u)));
    desc.TMin = 0.f;
    desc.TMax = bounce_range;
    TRACE_NEAREST(query, desc, EVERYTHING)
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) continue;
    float3 hit_light;
    HitLight(desc.Origin + desc.Direction * query.CommittedRayT(), hit_light);
    light += hit_light;
  }
  light /= max(bounce_rays, 1u);

  // Last frame's accumulation where this surface was (reset where it was not: disocclusions, cuts).
  float4 previous = 0.f;
  int2 previous_pixel;
  if ((flags & BOUNCE_HISTORY) != 0u && PreviousPixel(position.xyz, previous_pixel) && SeenLastFrame(position.xyz, previous_pixel)) {
    previous = history[previous_pixel];
  }
  const float frames = min(previous.w + 1.f, MAX_FRAMES);
  accumulated[id.xy] = float4(lerp(previous.rgb, light, 1.f / frames), frames);
}
