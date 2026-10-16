// summedareatable.hlsl cs_blur_hashed / cs_masked_blur_hashed (DISABLE_SAT: 30 random taps in a disk), the depth of
// field's background / foreground blur, written from the game's source. Shared by dofblur00 (0x6449D186), dofblur01
// (0x32876DB2) and dofblur02 (0x6531CCC3, cs_blur_hashed_unorm, DOF_BLUR_UNORM: an 8-bit output).
// Render scale (DLSS addon): the 3D image only fills the top-left render scale of these full-size textures (the
// addon's SI.render_scale_x / y, captured from the 3D passes' viewports), but the radius is counted in full-size
// texels: it blurred 1 / scale too much. The radius is scaled back, the taps stay inside the rendered part and the
// rest is skipped. At full size the same as the game's.
#include "../shared.h"

cbuffer cb_summed_area_table : register(b0)
{
  float g_summed_area_table_blur_kernel_radius : packoffset(c0);
  float g_summed_area_table_blur_kernel_radius_masked : packoffset(c0.y);
  float g_blur_timer : packoffset(c0.z);
}

SamplerState s_blur_size : register(s0);
SamplerState s_image : register(s1);
Texture2D<float4> t_image : register(t0);
Texture2D<float2> t_blur_size : register(t1);
Texture2D<float2> t_override_blur_size : register(t2);
#ifdef DOF_BLUR_UNORM
RWTexture2D<unorm float4> t_blur_output : register(u0);
#else
RWTexture2D<float4> t_blur_output : register(u0);
#endif

#define TAU 6.28318530718
#define HASHSCALE 443.8975

float2 RandBokeh(inout float2 r)
{
  r = frac(r * float2(33.3983, 43.4427));
  // Circular bokeh pattern.
  return sqrt(r.x + .001) * float2(sin(r.y * TAU), cos(r.y * TAU)) * .5;
}

float2 Rand2D(float2 p)
{
  float3 p3 = frac(float3(p.xyx) * HASHSCALE);
  p3 += dot(p3, p3.yzx + 19.19);
  return frac(float2((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y));
}

// The rendered fraction of the textures (1: full size, also when the addon has no value).
float2 RenderScale()
{
  const float2 scale = float2(SI.render_scale_x, SI.render_scale_y);
  return (scale.x > 0.f && scale.y > 0.f) ? min(scale, 1.f) : float2(1.f, 1.f);
}

// The pixel's blur radius in UV: blur size (x: proportion of the kernel, with the override) x the kernel's radius in
// texels / the width, x the render scale. HDR: x Depth of Field.
float BlurRadius(float2 uv, float blur_size, float screen_width)
{
  const float2 override_blur_size = t_override_blur_size.SampleLevel(s_blur_size, uv, 0);
  blur_size = lerp(max(blur_size, override_blur_size.x), override_blur_size.x, override_blur_size.y);
  const float blur_radius = blur_size * g_summed_area_table_blur_kernel_radius * (40.0f / 13.0f);
  const float strength = (HDR >= 1.f) ? SI.depth_of_field : 1.f;
  return blur_radius / screen_width * RenderScale().x * strength;
}
