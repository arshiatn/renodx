// ---- Rebuilt from dofbg00_0xC7EBC4DD.cs_5_0.cso (3Dmigoto couldn't decompile the UAV writes).

// WARHAMMER 3 dofbg00 - DOF background blur: 29 random taps in a disk over the background
// layer only (premultiplied: alpha = background coverage from dofcoc00). Writes over the
// sharp image where there is background; dof00 then does the foreground blur.
// Same fix as dof00: below 100% resolution scale (DLSS mod) the radius was counted in
// full-size texels (1/scale too big). Radius scaled back to native, taps stay inside the
// rendered area, the unused area is skipped. Scale comes from the addon.

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

  // Outside the rendered area: nothing to blur, keep what's there.
  if (scaled && any(uv > render_scale)) return;

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

  // Too small to see: unblurred background (stock). No background here: keep the image.
  if (blur < 1.f / max(image_size.x, image_size.y)) {
    const float4 background = t_image.Load(int3(vThreadID.xy, 0));
    if (background.w != 0.f) {
      t_blur_output[vThreadID.xy] = float4(background.xyz / background.w, 1);
    }
    return;
  }

  const float2 radius = blur * float2(output_size.y / output_size.x, 1);

  // Taps stay inside the rendered area.
  const float2 half_texel = 0.5 / image_size;
  const float2 tap_min = half_texel;
  const float2 tap_max = render_scale - half_texel;

  // Stock per-pixel hash (seeded by g_blur_timer, no frac here unlike dof00).
  float2 noise = frac((uv + g_blur_timer) * 443.897491);
  noise += dot(noise.xyx, noise.yxx + 19.1900005);
  noise = frac(noise * float2(noise.x + noise.y, noise.x + noise.x));

  float4 sum = 0;
  [loop]
  for (int i = 0; i < 29; ++i) {
    noise = frac(noise * float2(33.3983002, 43.4426994));
    const float distance = sqrt(noise.x + 0.00100000005);
    float s, c;
    sincos(6.28318548 * noise.y, s, c);
    float2 tap = (float2(s, c) * distance) * radius * 0.5 + uv;
    if (scaled) tap = clamp(tap, tap_min, tap_max);
    sum += t_image.SampleLevel(s_image_s, tap, 0);
  }

  // Premultiplied: divide by coverage. No background in reach: keep the image.
  if (sum.w == 0.f) return;
  t_blur_output[vThreadID.xy] = float4(sum.xyz / sum.w, 1);
}
