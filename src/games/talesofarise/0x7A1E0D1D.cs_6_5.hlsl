// Path tracing's learning at the lighting pass, before the paths (rt_cache.hlsli): one pixel in four (another each
// frame) writes its G-buffer base colour into its radiance cache cell; one in sixteen adds to the learned lighting's
// sums: y = last frame's scene colour there / its base colour, x = the sun's visibility (the ray-traced one when on)
// x N.L.
#include "./rt_cache.hlsli"

Texture2D<float4> normals : register(t6);     // G-buffer A: world normal x 0.5 + 0.5
Texture2D<float4> base_color : register(t14); // G-buffer C
Texture2D<float> sun_visibility : register(t15);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 size;
  positions.GetDimensions(size.x, size.y);
  const uint2 pixel = id.xy * 2u + uint2(frame & 1u, (frame >> 1u) & 1u);
  if (any(pixel >= size)) return;
  const float4 position = positions[pixel];
  const float3 encoded = normals[pixel].xyz;
  if (position.w <= 0.f || dot(encoded, encoded) == 0.f) return;
  const float3 base = base_color[pixel].rgb;
  CacheSetAlbedo(position.xyz, sun[uint2(1, 0)].xyz, base);

  if (((id.x ^ id.y ^ (frame >> 2u)) & 3u) != 0u || (flags & HISTORY_VALID) == 0u) return;
  if (dot(base, float3(0.2126f, 0.7152f, 0.0722f)) < 0.04f) return;
  int2 previous_pixel;
  if (!PreviousPixel(position.xyz, previous_pixel) || !SeenLastFrame(position.xyz, previous_pixel)) return;
  const float3 y = min(max(previous_color[previous_pixel].rgb, 0.f) / max(base, 0.05f), LIGHT_FIT_MAX_Y);
  const float4 toward_sun = sun[uint2(0, 0)];
  const float visibility = ((flags & SUN_SHADOWS) != 0u) ? sun_visibility[pixel] : 1.f;
  const float x = (toward_sun.w != 0.f) ? visibility * saturate(dot(normalize(encoded * 2.f - 1.f), toward_sun.xyz)) : 0.f;
  const uint fixed_x = (uint)(x * LIGHT_FIT_X + 0.5f);
  const uint3 fixed_y = (uint3)(y * LIGHT_FIT_Y + 0.5f);
  const uint3 fixed_xy = (uint3)(x * y * LIGHT_FIT_Y + 0.5f);
  InterlockedAdd(light_fit[0], 1u);
  InterlockedAdd(light_fit[1], fixed_x);
  InterlockedAdd(light_fit[2], (uint)(x * x * LIGHT_FIT_X + 0.5f));
  InterlockedAdd(light_fit[3], fixed_y.r);
  InterlockedAdd(light_fit[4], fixed_y.g);
  InterlockedAdd(light_fit[5], fixed_y.b);
  InterlockedAdd(light_fit[6], fixed_xy.r);
  InterlockedAdd(light_fit[7], fixed_xy.g);
  InterlockedAdd(light_fit[8], fixed_xy.b);
}
