// ---- Rebuilt from dof00_0x6307144B.cs_5_0.cso (3Dmigoto couldn't decompile the UAV writes).

// WARHAMMER 3 dof00 - depth of field: 30 random taps in a disk (also the UI background blur).
// With a resolution scale < 100% (DLSS mod: Ultra Performance = 33%) the frame only fills
// the top-left part of these full-size textures, but the radius is counted in full-size
// texels, so it blurred 1/scale too much. Radius is scaled back to native, taps stay inside
// the rendered area, and the unused area is skipped. Scale comes from the addon.

cbuffer cb_summed_area_table : register(b0)
{
  float g_summed_area_table_blur_kernel_radius : packoffset(c0);
  float g_summed_area_table_blur_kernel_radius_masked : packoffset(c0.y);
  float g_blur_timer : packoffset(c0.z);
}

SamplerState s_blur_size_s : register(s0);
SamplerState s_image_s : register(s1);
Texture2D<float4> t_image : register(t0);
Texture2D<float2> t_blur_size : register(t1);
Texture2D<float2> t_override_blur_size : register(t2);
RWTexture2D<float4> t_blur_output : register(u0);

#include "../shared.h"

// Rendered fraction of the targets (1 = native / unknown).
float2 RenderScale()
{
  float2 scale = float2(SI.render_scale_x, SI.render_scale_y);
  return (scale.x > 0.f && scale.y > 0.f) ? min(scale, 1.f) : 1.f;
}

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  uint output_width, output_height;
  t_blur_output.GetDimensions(output_width, output_height);
  const float2 output_size = float2(output_width, output_height);
  if (any(vThreadID.xy >= uint2(output_width, output_height))) return;

  const float2 uv = ((float2)vThreadID.xy + 0.5) / output_size;

  uint image_width, image_height, image_levels;
  t_image.GetDimensions(0, image_width, image_height, image_levels);
  const float2 image_size = float2(image_width, image_height);

  const float2 render_scale = RenderScale();
  const bool scaled = any(render_scale < 0.999f);

  // Outside the rendered area: stale data nobody reads, just copy it.
  if (scaled && any(uv > render_scale)) {
    t_blur_output[vThreadID.xy] = float4(t_image.Load(int3(vThreadID.xy, 0)).xyz, 1);
    return;
  }

  // Blur size (depth of field), maxed / lerped with the UI override.
  float blur = t_blur_size.SampleLevel(s_blur_size_s, uv, 0).x;
  const float2 override_blur = t_override_blur_size.SampleLevel(s_blur_size_s, uv, 0).xy;
  blur = max(blur, override_blur.x);
  blur = override_blur.y * (override_blur.x - blur) + blur;
  blur = blur * ((HDR >= 0.5f) ? SI.dof : 1.f);  // DOF slider (HDR only): 0 = off, like in-game off
  blur = blur * g_summed_area_table_blur_kernel_radius;
  blur = blur * 3.07692313;
  blur = blur / output_size.x;
  blur = blur * render_scale.y;  // full-size texels -> same size on screen as native

  // Too small to see: unblurred (stock).
  if (blur < 1.f / max(image_size.x, image_size.y)) {
    t_blur_output[vThreadID.xy] = float4(t_image.Load(int3(vThreadID.xy, 0)).xyz, 1);
    return;
  }

  const float2 radius = blur * float2(output_size.y / output_size.x, 1);

  // Taps stay inside the rendered area.
  const float2 half_texel = 0.5 / image_size;
  const float2 tap_min = half_texel;
  const float2 tap_max = scaled ? (render_scale - half_texel) : (1.f - half_texel);

  // Stock per-pixel hash (animated by g_blur_timer).
  float2 noise = frac((uv + frac(g_blur_timer)) * 443.897491);
  noise += dot(noise.xyx, noise.yxx + 19.1900005);
  noise = frac(noise * float2(noise.x + noise.y, noise.x + noise.x));

  float3 sum = 0;
  [loop]
  for (int i = 0; i < 30; ++i) {
    noise = frac(noise * float2(33.3983002, 43.4426994));
    const float distance = sqrt(noise.x + 0.00100000005);
    float s, c;
    sincos(6.28318548 * noise.y, s, c);
    float2 tap = (float2(s, c) * distance) * radius * 0.5 + uv;
    if (scaled) tap = clamp(tap, tap_min, tap_max);
    sum += t_image.SampleLevel(s_image_s, tap, 0).xyz;
  }

  t_blur_output[vThreadID.xy] = float4(sum * 0.0333333351, 1);
}
