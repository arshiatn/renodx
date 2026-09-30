// ---- Rebuilt from 3Dmigoto v1.3.16 output (thread group / structured writes were missing).

// WARHAMMER 3 doffocus00 - DOF auto focus: nearest depth in a small cross around each focus
// point (32 max), then a moving average of the focus depth / width into t_focal_depth[0].
// Fixes:
//  - Render scale (DLSS mod): focus points (0-1) were scaled by the full texture size, but the
//    frame only fills the viewport part of it -> focus read stale depth. Now the viewport.
//  - Resolution: the cross radius is in pixels -> 1080p pixels, like the tilt-shift.

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
RWStructuredBuffer<Focal_info> t_focal_depth : register(u0);

groupshared float g_point_depth[32];

// Rendered part (viewport) of the full-size targets. Native: the whole texture.
float2 RenderRect(float2 texture_size)
{
  const float2 rect = g_viewport_dimensions.xy;
  return (rect.x >= 1.f && rect.y >= 1.f) ? min(rect, texture_size) : texture_size;
}

float LinearDepth(float raw)
{
  return dot(float2(raw, 1), inv_projection._m22_m32) / dot(float2(raw, 1), inv_projection._m23_m33);
}

[numthreads(32, 1, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  if (vThreadID.x < g_used_focal_points) {
    uint width, height, levels;
    t_depth.GetDimensions(0, width, height, levels);
    const float2 rect = RenderRect(float2(width, height));

    const float2 focus_point = floor(g_focal_points[vThreadID.x] * rect);
    const int2 center = (int2)focus_point;
    const int radius = (int)round((float)g_focal_cross_radius * (rect.y / 1080.f));
    const int2 start = center - radius;
    const float2 end = min(rect - 1.f, (float2)(uint2)(center + radius));

    // Nearest depth on the cross (stock: horizontal inclusive, vertical exclusive).
    float nearest = 1.f;
    for (int x = start.x; (float)x <= end.x; ++x) {
      nearest = min(t_depth.Load(int3(max(x, 0), max(center.y, 0), 0)).x, nearest);
    }
    for (int y = start.y; (float)y < end.y; ++y) {
      nearest = min(t_depth.Load(int3(max(center.x, 0), max(y, 0), 0)).x, nearest);
    }
    g_point_depth[vThreadID.x] = nearest;
  }
  GroupMemoryBarrierWithGroupSync();

  // No focus points / reset: fixed range from the cbuffer (stock).
  if (g_reset_focal_depths || (vThreadID.x == 0 && g_used_focal_points == 0u)) {
    const float middle = 0.5f * (g_dof_focal_depth_near + g_dof_focal_depth_far);
    t_focal_depth[0].focal_depth = middle;
    t_focal_depth[0].focal_width = middle;
    t_focal_depth[0].focal_depth_near = g_dof_focal_depth_near;
    t_focal_depth[0].focal_depth_near_max = g_dof_focal_depth_near;
    t_focal_depth[0].focal_depth_far = g_dof_focal_depth_far;
    t_focal_depth[0].focal_depth_far_max = g_dof_focal_depth_far_max;
    return;
  }

  if (vThreadID.x == 0) {
    float nearest = g_point_depth[0];
    float farthest = g_point_depth[0];
    for (uint i = 1; i < g_used_focal_points; ++i) {
      nearest = min(nearest, g_point_depth[i]);
      farthest = max(farthest, g_point_depth[i]);
    }
    const float depth_a = LinearDepth(nearest);
    const float depth_b = min(4000.f, LinearDepth(farthest));
    const float width = g_focal_width_dilation * abs(depth_b - depth_a);

    // Moving average (stock).
    const float keep = 1.f - g_focal_depth_moving_average_weight;
    const float focal_depth = (depth_b + depth_a) * g_focal_depth_moving_average_weight * 0.5f
                              + keep * t_focal_depth[0].focal_depth;
    t_focal_depth[0].focal_depth = focal_depth;
    const float focal_width = g_focal_depth_moving_average_weight * width + keep * t_focal_depth[0].focal_width;

    const float transition = abs(g_dof_focal_depth_far_max - g_dof_focal_depth_far);
    const float near = focal_depth - 0.5f * focal_width;
    const float far = focal_depth + 0.5f * focal_width;
    t_focal_depth[0].focal_width = focal_width;
    t_focal_depth[0].focal_depth_near = near;
    t_focal_depth[0].focal_depth_near_max = near - transition;
    t_focal_depth[0].focal_depth_far = far;
    t_focal_depth[0].focal_depth_far_max = far + transition;
  }
}
