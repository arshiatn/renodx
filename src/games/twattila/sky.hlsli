#ifndef SI
#include "./shared.h"
#endif

// skydome.hlsl (ps30_main, ps30_gbuffer): the sky cube, banded (its gradients are compressed). Sky Debanding
// (SI.sky_deband 1-3, low / medium / high, as Pharaoh's sky): 12 taps on two rings around the view direction (24 and 64
// cube texels, the second turned by 30 degrees), each weighted by how close its colour is to the centre's (within 6 /
// 11 / 20 code values, in a square-root encoding: cheaper than sRGB's, about as even), then 4 code values of
// screen-space dither. Half the taps of v55-v58 (ATN: too heavy). Off: the game's.

cbuffer camera_VS_PS : register(b0)
{
  float3 camera_position : packoffset(c0);
  float4x4 view : packoffset(c1);
  float4x4 projection : packoffset(c5);
  float4x4 view_projection : packoffset(c9);
  float4x4 inv_view : packoffset(c13);
  float4x4 inv_projection : packoffset(c17);
  float4x4 inv_view_projection : packoffset(c21);
  float4 camera_near_far : packoffset(c25);
  float time_in_sec : packoffset(c26);
  float2 g_inverse_focal_length : packoffset(c26.y);
  float g_vertical_fov : packoffset(c26.w);
  float4 g_screen_size : packoffset(c27);
  float g_vpos_texel_offset : packoffset(c28);
  float4 g_viewport_dimensions : packoffset(c29);
  float4 g_render_target_dimensions : packoffset(c30);
  float4 g_camera_temp0 : packoffset(c31);
  float4 g_camera_temp1 : packoffset(c32);
  float4 g_camera_temp2 : packoffset(c33);
  float4 g_clip_rect : packoffset(c34);
  int g_num_of_samples : packoffset(c35);
}

SamplerState s_sky_s : register(s0);
TextureCube<float4> s_sky : register(t0);

static const float SKY_DEBAND_RADIUS_TEXELS = 64.f;
static const float SKY_DITHER_CODES = 4.f;

// The sky towards `direction` (from the camera; below the horizon the horizon's, as the game's) at screen `pixel`.
float4 SkySample(float3 direction, float2 pixel)
{
  const float3 view_direction = normalize(direction);
  // The gradients from the whole quad, before the branch.
  const float3 gradient_x = ddx(view_direction);
  const float3 gradient_y = ddy(view_direction);
  float4 color = s_sky.Sample(s_sky_s, float3(view_direction.x, max(0.f, view_direction.y), view_direction.z));
  [branch]
  if (SI.sky_deband > 0.f)
  {
    float threshold_codes = 6.f;
    if (SI.sky_deband >= 3.f) threshold_codes = 20.f;
    else if (SI.sky_deband >= 2.f) threshold_codes = 11.f;
    const float threshold = threshold_codes / 255.f;

    // A cube face spans 2 units at distance 1: 2 / its size per texel around the view direction.
    uint face_size, face_height;
    s_sky.GetDimensions(face_size, face_height);
    const float radius = SKY_DEBAND_RADIUS_TEXELS * 2.f / float(face_size);
    const float3 up = (abs(view_direction.y) < 0.99f) ? float3(0.f, 1.f, 0.f) : float3(1.f, 0.f, 0.f);
    const float3 tangent = normalize(cross(view_direction, up));
    const float3 bitangent = cross(view_direction, tangent);

    // Every tap compared with the centre (not the running average: no colours creeping in across edges).
    const float3 center_code = sqrt(max(color.rgb, 0.f));
    // Six directions 60 degrees apart; the outer ring turned by 30.
    const float2 directions[12] = {
      float2(1.f, 0.f), float2(0.5f, 0.8660254f), float2(-0.5f, 0.8660254f),
      float2(-1.f, 0.f), float2(-0.5f, -0.8660254f), float2(0.5f, -0.8660254f),
      float2(0.8660254f, 0.5f), float2(0.f, 1.f), float2(-0.8660254f, 0.5f),
      float2(-0.8660254f, -0.5f), float2(0.f, -1.f), float2(0.8660254f, -0.5f)
    };
    float3 sum = color.rgb;
    float total_weight = 1.f;
    [unroll]
    for (int index = 0; index < 12; ++index)
    {
      const float2 offset = directions[index] * (radius * ((index < 6) ? 0.375f : 1.f));
      float3 tap_direction = view_direction + tangent * offset.x + bitangent * offset.y;
      tap_direction.y = max(0.f, tap_direction.y);
      const float3 tap = s_sky.SampleGrad(s_sky_s, tap_direction, gradient_x, gradient_y).rgb;
      const float3 difference = abs(sqrt(max(tap, 0.f)) - center_code);
      const float error = max(difference.r, max(difference.g, difference.b));
      // One weight for all channels: full below half the threshold, none above it.
      const float weight = 1.f - smoothstep(0.5f * threshold, threshold, error);
      sum += tap * weight;
      total_weight += weight;
    }

    // Interleaved gradient noise, fixed to screen pixels, in code values of the same encoding.
    const float noise = frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f)))) - 0.5f;
    const float3 code = max(sqrt(max(sum / total_weight, 0.f)) + noise * (SKY_DITHER_CODES / 255.f), 0.f);
    color.rgb = code * code;
  }
  return color;
}
