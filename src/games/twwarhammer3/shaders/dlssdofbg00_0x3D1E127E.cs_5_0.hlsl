// ---- Rebuilt and fixed from 0x3D1E127E.cs_5_0 (DLSS / Dynamic Viewport Background Blur)

cbuffer Blur : register(b0)
{
  float radius : packoffset(c0);
  float masked_radius : packoffset(c0.y);
  float timer : packoffset(c0.z);
}

cbuffer CameraSnapshot : register(b13)
{
  float4 camera[66] : packoffset(c0);
}

SamplerState mask_sampler_s : register(s0);
SamplerState color_sampler_s : register(s1);
Texture2D<float4> color : register(t0);
Texture2D<float2> blur_size : register(t1);
Texture2D<float2> override_size : register(t2);
RWTexture2D<float4> result : register(u0);

#include "../shared.h"

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  uint output_width, output_height;
  result.GetDimensions(output_width, output_height);
  const float2 output_size = float2(output_width, output_height);
  if (any(vThreadID.xy >= uint2(output_width, output_height))) return;

  uint image_width, image_height;
  color.GetDimensions(image_width, image_height);
  const float2 image_size = float2(image_width, image_height);

  const bool valid_viewport = all(camera[39].xy >= 1.f) && all(image_size >= camera[39].xy);
  const float2 render_rect = valid_viewport ? floor(camera[39].xy) : image_size;
  const float2 render_scale = render_rect / image_size;
  const float2 uv = ((float2)vThreadID.xy + 0.5f) / output_size;

  const bool scaled = (render_rect.x < image_size.x || render_rect.y < image_size.y);

  // Outside the rendered area: nothing to blur
  if (any(uv > render_scale)) return;

  float blur = blur_size.SampleLevel(mask_sampler_s, uv, 0).x;
  const float2 override_blur = override_size.SampleLevel(mask_sampler_s, uv, 0).xy;
  blur = max(blur, override_blur.x);
  blur = override_blur.y * (override_blur.x - blur) + blur;

  // Injected Depth of Field slider
  blur = blur * ((HDR >= 0.5f) ? SI.dof : 1.f);

  blur = blur * radius;
  blur = blur * 3.07692313f;
  blur = blur / output_size.x;

  // Small blur or unblurred: keep background if it exists
  if (blur * render_scale.y < 1.f / max(image_size.x, image_size.y)) {
    const float4 background = color.Load(int3(vThreadID.xy, 0));
    if (background.w != 0.f) {
      result[vThreadID.xy] = float4(background.xyz / background.w, 1.f);
    }
    return;
  }

  const float2 tap_radius = blur * float2(output_size.y / output_size.x, 1.f) * render_scale;
  const float2 half_texel = 0.5f / image_size;
  const float2 tap_min = half_texel;
  const float2 tap_max = render_scale - half_texel;

  // Per-pixel noise
  float2 noise = frac((uv + timer) * 443.897491f);
  noise += dot(noise.xyx, noise.yxx + 19.1900005f);
  noise = frac(noise * float2(noise.x + noise.y, noise.x + noise.x));

  float4 sum = 0.f;
  [loop]
  for (int i = 0; i < 29; ++i) {
    noise = frac(noise * float2(33.3983002f, 43.4426994f));
    const float distance = sqrt(0.00100000005f + noise.x);
    float s, c;
    sincos(6.28318548f * noise.y, s, c);
    float2 tap = (float2(s, c) * distance) * tap_radius * 0.5f + uv;
    if (scaled) tap = clamp(tap, tap_min, tap_max);
    sum += color.SampleLevel(color_sampler_s, tap, 0);
  }

  if (sum.w != 0.f) {
    result[vThreadID.xy] = float4(sum.xyz / sum.w, 1.f);
  }
}