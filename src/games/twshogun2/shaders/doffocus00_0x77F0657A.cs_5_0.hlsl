// depthoffield.hlsl cs_compute_focal_depth (written from the game's source): the depth of field's auto focus, the
// nearest depth in a cross around each focus point, then a moving average of the focus depth / width.
// Render scale (DLSS addon): the focus points (0-1) were scaled by the full depth texture, but the 3D image only fills
// g_viewport_dimensions of it, so they read stale depth: now the viewport, the crosses kept inside it. At full size
// the same as the game's (the cross's left / top clamp is now signed: the game's wrapped as unsigned).
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
  float g_dof_focal_depth_near : packoffset(c0);
  float g_dof_focal_depth_far : packoffset(c0.y);
  float g_dof_focal_depth_near_max : packoffset(c0.z);
  float g_dof_focal_depth_far_max : packoffset(c0.w);
  uint g_used_focal_points : packoffset(c3.z);
  float2 g_focal_points[32] : packoffset(c4);
  uint g_focal_cross_radius : packoffset(c35.z);
  float g_focal_depth_moving_average_weight : packoffset(c35.w);
  float g_focal_width_dilation : packoffset(c36);
  bool g_reset_focal_depths : packoffset(c36.y);
}

Texture2D<float> t_depth : register(t0);
RWStructuredBuffer<Focal_info> t_focal_depth : register(u0);

groupshared float focal_depth_samples[32];

// camera.hlsl compute_eye_depth(float2(0.5, 0.5), depth): the screen centre, clip x, y = 0.
float EyeDepth(float clip_space_depth)
{
  float4 view_position = mul(float4(0.f, 0.f, clip_space_depth, 1.f), inv_projection);
  return view_position.z / view_position.w;
}

[numthreads(32, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
  // One thread per cross.
  if (dispatch_thread_id.x < g_used_focal_points)
  {
    float2 image_size;
    t_depth.GetDimensions(image_size.x, image_size.y);
    const int2 rendered = int2(min(g_viewport_dimensions.xy, image_size));
    const int2 focal_point = int2(floor(g_focal_points[dispatch_thread_id.x] * float2(rendered)));
    const int radius = int(g_focal_cross_radius);
    float min_depth = 1.0;
    // Horizontal bar.
    for (int i = max(0, focal_point.x - radius); i <= min(rendered.x - 1, focal_point.x + radius); ++i)
    {
      min_depth = min(t_depth[int2(i, focal_point.y)], min_depth);
    }
    // Vertical bar.
    for (int j = max(0, focal_point.y - radius); j < min(rendered.y - 1, focal_point.y + radius); ++j)
    {
      min_depth = min(t_depth[int2(focal_point.x, j)], min_depth);
    }
    focal_depth_samples[dispatch_thread_id.x] = min_depth;
  }

  GroupMemoryBarrierWithGroupSync();

  // No points: the focus from the constant buffer.
  if ((g_used_focal_points == 0 || g_reset_focal_depths) && dispatch_thread_id.x == 0)
  {
    t_focal_depth[0].focal_depth = (g_dof_focal_depth_near + g_dof_focal_depth_far) / 2;
    t_focal_depth[0].focal_width = t_focal_depth[0].focal_depth;
    t_focal_depth[0].focal_depth_near = g_dof_focal_depth_near;
    t_focal_depth[0].focal_depth_near_max = g_dof_focal_depth_near_max;
    t_focal_depth[0].focal_depth_far = g_dof_focal_depth_far;
    t_focal_depth[0].focal_depth_far_max = g_dof_focal_depth_far_max;
    return;
  }

  // The first thread: min / max of the crosses, focus depth and width (moving averages).
  if (dispatch_thread_id.x == 0)
  {
    float min_sample = focal_depth_samples[0];
    float max_sample = focal_depth_samples[0];
    for (uint i = 1; i < g_used_focal_points; ++i)
    {
      min_sample = min(min_sample, focal_depth_samples[i]);
      max_sample = max(max_sample, focal_depth_samples[i]);
    }
    const float weight = g_focal_depth_moving_average_weight;
    const float focal_depth_min = EyeDepth(min_sample);
    const float focal_depth_max = min(EyeDepth(max_sample), 4000.0);
    const float focal_width = abs(focal_depth_max - focal_depth_min) * g_focal_width_dilation;
    const float focal_depth = (focal_depth_max + focal_depth_min) / 2.0f;

    t_focal_depth[0].focal_depth = weight * focal_depth + (1 - weight) * t_focal_depth[0].focal_depth;
    t_focal_depth[0].focal_width = weight * focal_width + (1 - weight) * t_focal_depth[0].focal_width;

    const float far_falloff = abs(g_dof_focal_depth_far_max - g_dof_focal_depth_far);
    const float near_falloff = abs(g_dof_focal_depth_near - g_dof_focal_depth_near_max);

    t_focal_depth[0].focal_depth_near = t_focal_depth[0].focal_depth - (t_focal_depth[0].focal_width / 2.0);
    t_focal_depth[0].focal_depth_near_max = t_focal_depth[0].focal_depth - (t_focal_depth[0].focal_width / 2.0) - near_falloff;
    t_focal_depth[0].focal_depth_far = t_focal_depth[0].focal_depth + (t_focal_depth[0].focal_width / 2.0);
    t_focal_depth[0].focal_depth_far_max = t_focal_depth[0].focal_depth + (t_focal_depth[0].focal_width / 2.0) + far_falloff;
  }
}
