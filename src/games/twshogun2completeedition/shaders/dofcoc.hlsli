// depthoffield.hlsl cs_coc_bokeh, written from the game's source: the depth of field's circle of confusion and its
// foreground / background images (the blur passes then write them over the scene). Shared by dofcoc00 (0xB25F26C7,
// cs_coc_bokeh_high, DOF_COC_BOKEH_HIGH: bokeh hotspots) and dofcoc01 (0xB333A1B3, cs_coc_bokeh_low).
// The scene is read through the heat haze's offsets (depthoffieldshared.hlsl distort_pixel_index): with depth of field
// on, the haze is drawn into the scene here as well, before DLSS (the tonemap distorts it again). Truncated to whole
// pixels, so only strong haze shows (ATN: haze stayed over burning houses at Heat Haze 0). HDR: x VFX Distortion: Heat
// Haze. Render scale (DLSS addon): the offsets are in screen UV, but the image only fills the top-left render scale of
// the full-size textures: x the scale. At full size and Heat Haze 1 the same as the game's.
#include "../shared.h"

struct Focal_info
{
  float focal_depth;
  float focal_width;
  float focal_depth_near;
  float focal_depth_near_max;
  float focal_depth_far;
  float focal_depth_far_max;
};

cbuffer camera_VS_PS : register(b0)
{
  float4x4 inv_projection : packoffset(c26);
  float4 g_viewport_dimensions : packoffset(c38);
}

cbuffer Depth_of_field_cb : register(b1)
{
  float g_dof_bokeh_sprite_radius : packoffset(c1);
  float g_dof_bokeh_threshold : packoffset(c1.y);
  float2 g_dof_tilt_shift_angles : packoffset(c3);
}

Texture2D<float4> t_image : register(t0);
Texture2D<float> t_depth : register(t1);
Texture2D<float4> t_distortion_srv : register(t2);
RWStructuredBuffer<Focal_info> t_focal_depth : register(u0);
RWTexture2D<float> t_coc_bokeh_foreground : register(u1);
RWTexture2D<float> t_coc_bokeh_background : register(u2);
RWTexture2D<float4> t_forground_image_output : register(u3);
RWTexture2D<float4> t_background_image_output : register(u4);

#ifdef DOF_COC_BOKEH_HIGH
struct Bokeh_struct
{
  float4 m_info;
  float4 m_color;
};

SamplerState s_image_sampler : register(s0);
Texture2D<float4> t_image_lowres : register(t3);
AppendStructuredBuffer<Bokeh_struct> t_bokeh_hotspots_append : register(u5);
RWByteAddressBuffer t_bokeh_hotspots_draw_indirect : register(u6);
#endif

[numthreads(32, 32, 1)]
void main(int3 dispatch_thread_id : SV_DispatchThreadID)
{
  const int2 out_pixel_index = dispatch_thread_id.xy;

  // distort_pixel_index: 0.01 x the offset in UV, in the distortion texture's pixels.
  float2 render_scale = float2(SI.render_scale_x, SI.render_scale_y);
  render_scale = (render_scale.x > 0.f && render_scale.y > 0.f) ? min(render_scale, 1.f) : float2(1.f, 1.f);
  const float heat_haze = (HDR >= 1.f) ? SI.heat_haze : 1.f;
  float2 distortion_size;
  t_distortion_srv.GetDimensions(distortion_size.x, distortion_size.y);
  const float2 uv_bias = t_distortion_srv[out_pixel_index].xy * 0.01f * heat_haze;
  int2 pixel_index = out_pixel_index + uv_bias * distortion_size * render_scale;
  pixel_index = clamp(pixel_index, 0, g_viewport_dimensions.xy - 1);

  float2 image_size;
  t_image.GetDimensions(image_size.x, image_size.y);
  const float4 center_color = t_image[pixel_index];
  const float center_depth = t_depth[pixel_index];

  // compute_eye_depth
  const float2 clip_xy = (pixel_index / image_size) * float2(2, -2) + float2(-1, 1);
  const float4 pos_in_view = mul(float4(clip_xy, center_depth, 1), inv_projection);
  const float linear_center_depth = pos_in_view.z / pos_in_view.w;

  // compute_circle_of_confusion_tilt_shift
  const float2 normalise_coords = (float2(pixel_index) - (image_size / 2.0f)) / image_size;
  const float2 extra_depth = normalise_coords * sin(g_dof_tilt_shift_angles) * (image_size / 2.0);
  const float modified_linear_depth_far = linear_center_depth + extra_depth.x + extra_depth.y;
  const float modified_linear_depth_near = linear_center_depth - extra_depth.x - extra_depth.y;
  const float far = smoothstep(t_focal_depth[0].focal_depth_far, t_focal_depth[0].focal_depth_far_max, modified_linear_depth_far);
  const float near = 1 - smoothstep(t_focal_depth[0].focal_depth_near_max, t_focal_depth[0].focal_depth_near, modified_linear_depth_near);
  const float coc = max(near, far);

#ifdef DOF_COC_BOKEH_HIGH
  // compute_bokeh_hotspot_fast: brighter than the low-res image around it.
  const float center_average_intensity = (center_color.x + center_color.y + center_color.z) / 3;
  const float4 average_sample = t_image_lowres.SampleLevel(s_image_sampler, pixel_index / image_size, 0);
  const float average_intensity = (average_sample.x + average_sample.y + average_sample.z) / 3;
  const bool is_hotspot = min(center_average_intensity / average_intensity, 49.99f) >= g_dof_bokeh_threshold;
  if (coc > 0.1f && pixel_index.x < g_viewport_dimensions.x && pixel_index.y < g_viewport_dimensions.y && is_hotspot)
  {
    Bokeh_struct bokeh;
    bokeh.m_info = float4(out_pixel_index.xy, center_depth, max(coc * g_dof_bokeh_sprite_radius, 0.5f));
    bokeh.m_color = center_color;
    t_bokeh_hotspots_draw_indirect.InterlockedAdd(4, 1);
    t_bokeh_hotspots_append.Append(bokeh);
  }
#endif

  // Foreground and background images: alpha 1 where the pixel counts in the blur.
  t_forground_image_output[out_pixel_index] = float4(center_color.rgb, 1.0);
  if (linear_center_depth < t_focal_depth[0].focal_depth_near)
  {
    t_background_image_output[out_pixel_index] = 0;
    t_coc_bokeh_foreground[out_pixel_index] = coc;
    t_coc_bokeh_background[out_pixel_index] = 0;
  }
  else if (linear_center_depth < t_focal_depth[0].focal_depth_far)
  {
    t_background_image_output[out_pixel_index] = 0;
    t_coc_bokeh_foreground[out_pixel_index] = 0;
    t_coc_bokeh_background[out_pixel_index] = 0;
  }
  else
  {
    t_background_image_output[out_pixel_index] = float4(center_color.rgb, 1.0);
    t_coc_bokeh_foreground[out_pixel_index] = 0;
    t_coc_bokeh_background[out_pixel_index] = coc;
  }
}
