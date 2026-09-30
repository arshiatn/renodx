// ---- Rebuilt from 3Dmigoto v1.3.16 output (thread group / UAV writes were missing).

// WARHAMMER 3 dofcoc00 - DOF blur amount (circle of confusion) per pixel from the focus range
// (doffocus00), split into foreground / background. Heat-haze distortion is applied here too.
// Fixes:
//  - Render scale (DLSS mod): the frame only fills the viewport part of these full-size
//    textures. Stock used the full size, so the tilt-shift centre sat outside the picture.
//  - Resolution: tilt-shift was in pixels (2x stronger at 4K than 1080p) -> 1080p pixels.

struct Focal_info
{
  float focal_depth;
  float focal_width;
  float focal_depth_near;
  float focal_depth_near_max;
  float focal_depth_far;
  float focal_depth_far_max;
};

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
}

cbuffer Depth_of_field_cb : register(b1)
{
  float g_dof_focal_depth_near : packoffset(c0);
  float g_dof_focal_depth_far : packoffset(c0.y);
  float g_dof_focal_depth_near_max : packoffset(c0.z);
  float g_dof_focal_depth_far_max : packoffset(c0.w);
  float g_dof_bokeh_sprite_radius : packoffset(c1);
  float g_dof_bokeh_threshold : packoffset(c1.y);
  float g_dof_bokeh_hotspot_kernel_radius : packoffset(c1.z);
  float g_dof_bokeh_intensity : packoffset(c1.w);
  float g_dof_background_blur_kernel_radius : packoffset(c2);
  float g_dof_foreground_blur_kernel_radius : packoffset(c2.y);
  float g_dof_foreground_coc_dilation_kernel_radius : packoffset(c2.z);
  float2 g_dof_tilt_shift_angles : packoffset(c3);
  uint g_used_focal_points : packoffset(c3.z);
  float2 g_focal_points[32] : packoffset(c4);
  uint g_focal_cross_radius : packoffset(c35.z);
  float g_focal_depth_moving_average_weight : packoffset(c35.w);
  float g_focal_width_dilation : packoffset(c36);
  bool g_reset_focal_depths : packoffset(c36.y);
  float g_coc_bokeh_extraction_distance_modifier_percent : packoffset(c36.z);
  bool g_using_mboit_depth_buffer : packoffset(c36.w);
  bool g_depth_cutoff : packoffset(c37);
}

Texture2D<float4> t_depth : register(t0);
Texture2D<float4> t_distortion_srv : register(t1);
Texture2D<float2> t_mboit_depth_buffer_srv : register(t2);
Texture2D<float4> t_image : register(t3);
RWStructuredBuffer<Focal_info> t_focal_depth : register(u0);
RWTexture2D<float> t_coc_bokeh_foreground : register(u1);
RWTexture2D<float> t_coc_bokeh_background : register(u2);
RWTexture2D<float4> t_forground_image_output : register(u3);
RWTexture2D<float4> t_background_image_output : register(u4);

#include "../shared.h"

// Rendered part (viewport) of the full-size targets. Native: the whole texture.
float2 RenderRect(float2 texture_size)
{
  const float2 rect = g_viewport_dimensions.xy;
  return (rect.x >= 1.f && rect.y >= 1.f) ? min(rect, texture_size) : texture_size;
}

float LinearDepth(float2 ndc, float raw)
{
  const float4 position = float4(ndc, raw, 1);
  return dot(position, inv_projection._m02_m12_m22_m32) / dot(position, inv_projection._m03_m13_m23_m33);
}

[numthreads(32, 32, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  uint distortion_width, distortion_height, distortion_levels;
  t_distortion_srv.GetDimensions(0, distortion_width, distortion_height, distortion_levels);
  const float2 distortion_size = float2(distortion_width, distortion_height);
  uint image_width, image_height, image_levels;
  t_image.GetDimensions(0, image_width, image_height, image_levels);
  const float2 image_size = float2(image_width, image_height);
  const float2 rect = RenderRect(image_size);

  // Heat-haze distortion: offset (0-1 of the picture) -> pixels of the rendered part.
  const float4 distortion = t_distortion_srv.Load(int3(vThreadID.xy, 0));
  float2 offset = distortion.xy / ((distortion.z < 0.00100000005) ? 1.f : distortion.z);
  offset = offset * (1.f - distortion.w);
  offset = offset - distortion.w * offset;
  const float2 distortion_rect = distortion_size * (rect / image_size);
  const float2 coords = offset * distortion_rect * 0.0199999996 + (float2)vThreadID.xy;
  const int2 pixel = clamp((int2)coords, 0, (int2)rect - 1);  // stays inside the picture

  const float3 image = t_image.Load(int3(pixel, 0)).xyz;
  const float2 uv = (float2)pixel / rect;
  const float2 ndc = uv * float2(2, -2) + float2(-1, 1);
  float depth = LinearDepth(ndc, t_depth.Load(int3(pixel, 0)).x);
  const float mboit_depth = LinearDepth(ndc, t_mboit_depth_buffer_srv.Load(int3(pixel, 0)).x);
  depth = g_using_mboit_depth_buffer ? min(depth, mboit_depth) : depth;

  // Tilt-shift: centred on the picture, in 1080p pixels (stock: full texture, native pixels).
  const float2 centered = ((float2)pixel - 0.5f * rect) / rect;
  const float2 half_size_1080p = 0.5f * rect * (1080.f / rect.y);
  const float2 tilt = centered * sin(g_dof_tilt_shift_angles.xy) * half_size_1080p;
  const float depth_plus = depth + tilt.x + tilt.y;
  const float depth_minus = depth - tilt.x - tilt.y;

  // Blur amount: 1 before near_max / after far_max, 0 inside near..far (stock).
  const Focal_info focal = t_focal_depth[0];
  const float inv_near = 1.f / (focal.focal_depth_near - focal.focal_depth_near_max);
  const float inv_far = 1.f / (focal.focal_depth_far_max - focal.focal_depth_far);
  const float near_t = saturate((depth_minus - focal.focal_depth_near_max) * inv_near);
  const float far_t = saturate((depth_plus - focal.focal_depth_far) * inv_far);
  const float near_coc = 1.f - near_t * near_t * (3.f - 2.f * near_t);
  const float far_coc = far_t * far_t * (3.f - 2.f * far_t);
  float coc = max(near_coc, far_coc);
  if (g_depth_cutoff) coc *= saturate(depth * -0.00249999994 + 1.f);

  // Foreground / in focus / background split (stock).
  const float split_depth = depth * (1.f + g_coc_bokeh_extraction_distance_modifier_percent);
  float foreground_coc = 0.f;
  float background_coc = coc;
  float4 background = 0.f;
  if (split_depth < focal.focal_depth_near) {
    foreground_coc = coc;
    background_coc = 0.f;
  } else if (split_depth < focal.focal_depth_far) {
    background_coc = 0.f;
  } else {
    background = float4(image, 1.f);
  }

  t_background_image_output[vThreadID.xy] = background;
  t_forground_image_output[vThreadID.xy] = float4(image, 1.f);
  t_coc_bokeh_background[vThreadID.xy] = background_coc;
  t_coc_bokeh_foreground[vThreadID.xy] = foreground_coc;
}
