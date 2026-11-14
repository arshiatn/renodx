// autoexposurecs.hlsl cs_compute_tone_mapper_brightness: the auto exposure (luma along crosses at focus points,
// adapted over time), written from the game's source.
// Render scale (DLSS addon): it reads the 3D image (the DLSS input), drawn into the top-left render scale part of this
// full-size texture (SI.render_scale_x / y, captured from the 3D passes' viewports), but placed its points and crosses
// over the whole texture: they are placed in the rendered part (at full size the same as the game's).
// Modded Auto Exposure (Experimental, Attila's rows): the game meters the mean luma along its crosses, so the sun, fires and
// sparks on a cross darken the whole picture. With it on each sample counts at most the luma the last frame's exposure was
// set for (2^target / brightness^2, the inverse of its target) x 2^stops; the picture itself is untouched. Off: the game's.
#include "../shared.h"

#define MAX_FOCAL_POINTS 32

struct Auto_exposure_output
{
  float tone_mapper_brightness;
  float tone_mapper_debug_average_ev;
  float tone_mapper_debug_raw_target_brightness;
  float tone_mapper_debug_unused;
};

cbuffer tone_mapping : register(b0)
{
  float g_tone_mapping_brightness : packoffset(c0);
}

cbuffer Auto_exposure_cb : register(b1)
{
  uint g_used_focal_points : packoffset(c0);
  float2 g_focal_points[MAX_FOCAL_POINTS] : packoffset(c1);
  uint g_focal_cross_radius : packoffset(c32.z);
  float g_target_exposure : packoffset(c32.w);
  float2 g_adaptation_speed : packoffset(c33);
  float g_max_relative_correction : packoffset(c33.z);
  float g_sensitivity : packoffset(c33.w);
  float g_delta_time : packoffset(c34);
}

Texture2D<float4> t_light_accumulation : register(t0);
RWStructuredBuffer<Auto_exposure_output> g_auto_exposure_output_buffer : register(u0);

groupshared float focal_samples[MAX_FOCAL_POINTS];

[numthreads(MAX_FOCAL_POINTS, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
  if (dispatch_thread_id.x < g_used_focal_points)
  {
    float2 texture_size;
    t_light_accumulation.GetDimensions(texture_size.x, texture_size.y);
    float2 render_scale = float2(SI.render_scale_x, SI.render_scale_y);
    render_scale = (render_scale.x > 0.f && render_scale.y > 0.f) ? min(render_scale, 1.f) : float2(1.f, 1.f);
    const float2 image_size = floor(texture_size * render_scale);
    const int radius = int(round(g_focal_cross_radius * render_scale.x));

    const int2 focal_point = floor(g_focal_points[dispatch_thread_id.x] * image_size);
    // Read before thread 0 writes this frame's (after the barrier below).
    const float prev_brightness = g_auto_exposure_output_buffer[0].tone_mapper_brightness;
    const bool capped = SI.auto_exposure_highlight_protection > 0.f && prev_brightness > 0.000001f;
    const float luma_cap = exp2(g_target_exposure + clamp(SI.auto_exposure_highlight_headroom_stops, 1.f, 8.f))
                           / (prev_brightness * prev_brightness);
    float average_luma = 0.0;
    float num_samples = 0;
    for (int i = max(0, focal_point.x - radius); i <= min(image_size.x - 1, focal_point.x + radius); ++i)
    {
      const float luma = renodx::color::y::from::BT709(t_light_accumulation[uint2(i, focal_point.y)].rgb);
      average_luma += capped ? min(luma, luma_cap) : luma;
      num_samples += 1;
    }
    for (int j = max(0, focal_point.y - radius); j < min(image_size.y - 1, focal_point.y + radius); ++j)
    {
      const float luma = renodx::color::y::from::BT709(t_light_accumulation[uint2(focal_point.x, j)].rgb);
      average_luma += capped ? min(luma, luma_cap) : luma;
      num_samples += 1;
    }
    focal_samples[dispatch_thread_id.x] = average_luma / max(num_samples, 1);
  }

  GroupMemoryBarrierWithGroupSync();

  if (dispatch_thread_id.x == 0)
  {
    float averaged_luma = 0;
    for (uint i = 0; i < g_used_focal_points; ++i)
    {
      averaged_luma += focal_samples[i];
    }
    averaged_luma /= g_used_focal_points;

    const float prev_tone_mapper_brightness = g_auto_exposure_output_buffer[0].tone_mapper_brightness;

    float average_ev = 0.0f;
    float final_brightness = 1;
    float raw_target_brightness = 0.0f;
    if (prev_tone_mapper_brightness > 0.000001f)
    {
      average_ev = log2(averaged_luma);
      float target_brightness = sqrt(pow(2, g_target_exposure) / pow(2, average_ev));
      raw_target_brightness = target_brightness;

      target_brightness = clamp(target_brightness, g_tone_mapping_brightness - g_max_relative_correction,
                                g_tone_mapping_brightness + g_max_relative_correction);

      const float threshold = prev_tone_mapper_brightness * (1 - g_sensitivity);
      target_brightness = lerp(prev_tone_mapper_brightness, target_brightness,
                               saturate(abs(target_brightness - prev_tone_mapper_brightness) / threshold));

      final_brightness = (target_brightness > prev_tone_mapper_brightness)
                             ? min(prev_tone_mapper_brightness + g_delta_time * g_adaptation_speed.y, target_brightness)
                             : max(prev_tone_mapper_brightness - g_delta_time * g_adaptation_speed.x, target_brightness);
    }

    g_auto_exposure_output_buffer[0].tone_mapper_brightness = final_brightness;
    g_auto_exposure_output_buffer[0].tone_mapper_debug_average_ev = average_ev;
    g_auto_exposure_output_buffer[0].tone_mapper_debug_raw_target_brightness = raw_target_brightness;
  }
}
