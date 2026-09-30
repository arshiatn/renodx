// ---- Hand-decompiled from sky00_0xEFDBC227.ps_5_0.cso (no 3Dmigoto output).

// WARHAMMER 3 sky00 - skydome: flow-animated sky texture (2 phases), pole tint.
// Texture is BC6H (HDR), so banding comes from block compression, not 8-bit codes.
// Same idea as Pharaoh: threshold filter (24 taps) + static dither, HDR only.
// Thresholds/dither are relative (not sRGB codes) since the texture isn't 0..1.
// SI.sky_deband: 0 off (stock), 1 low, 2 medium, 3 high.

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
}

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_untextured : packoffset(c0.y);
  float g_rain : packoffset(c0.z);
  float g_tesselation_factor : packoffset(c0.w);
  float g_tesselation_displacement_scale : packoffset(c1);
  float2 g_uv2_tile_interval : packoffset(c1.y);
  float2 g_texel_density_uv_scale : packoffset(c2);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
  int g_use_secondary_lut : packoffset(c5);
  uint g_material_offset : packoffset(c5.y);
}

cbuffer cb_environment : register(b2)
{
  float2 g_wind_direction : packoffset(c0);
  float g_cubemap_sun_colour_scale : packoffset(c5);
}

cbuffer environment_PS : register(b3)
{
  float g_skydome_texture_weights[8] : packoffset(c32);
  uint g_skydome_texture_indices[8] : packoffset(c40);
  uint g_skydome_texture_count : packoffset(c47.y);
  bool g_skydome_texture_array_enabled : packoffset(c47.z);
}

cbuffer lighting_VS_PS : register(b4)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
}

struct AUTO_PARAMETERS
{
  float sp_flow_power;               // +0
  float sp_flow_speed;               // +4
  float sp_horizon_flow_fade_width;  // +8
  float sp_horizon_flow_start;       // +12
  float3 sp_pole_colour;             // +16
  float sp_pole_flow_mask_width;     // +28
  float sp_pole_tint_amount;         // +32
};

SamplerState s_skydome_texture_array_sampler_s : register(s0);
SamplerState s_xml_sampler_s : register(s1);
Texture2DArray<float4> t_skydome_texture_array : register(t0);
Texture2D<float4> t_xml_base_colour : register(t1);
StructuredBuffer<AUTO_PARAMETERS> xml_auto_array_parameters : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


static const float SKY_DEBAND_RADIUS_TEXELS = 64.0;
static const float SKY_DEBAND_THRESHOLD_LOW = 0.07;     // relative difference
static const float SKY_DEBAND_THRESHOLD_MEDIUM = 0.12;
static const float SKY_DEBAND_THRESHOLD_HIGH = 0.20;
static const float SKY_DITHER_AMOUNT = 0.02;            // relative, peak-to-peak


// Stock fetch: weighted texture-array blend, or the single XML texture.
float3 SkySampleStock(float2 uv)
{
  float3 sky = 0.f;
  [branch]
  if (g_skydome_texture_array_enabled) {
    [loop]
    for (uint i = 0; i < g_skydome_texture_count; i++) {
      uint index = g_skydome_texture_indices[i];
      float weight = g_skydome_texture_weights[i];
      if (index == 0xFFFFFFFFu) {
        sky += weight * float3(1, 0.41, 0.7);  // missing-texture colour (stock)
      } else {
        sky += t_skydome_texture_array.Sample(s_skydome_texture_array_sampler_s, float3(uv, (float)index)).xyz * weight;
      }
    }
  } else {
    sky = t_xml_base_colour.Sample(s_xml_sampler_s, uv).xyz;
  }
  return sky;
}

// Same fetch with explicit gradients (safe in the filter loop).
float3 SkySampleGrad(float2 uv, float2 dx, float2 dy)
{
  float3 sky = 0.f;
  [branch]
  if (g_skydome_texture_array_enabled) {
    [loop]
    for (uint i = 0; i < g_skydome_texture_count; i++) {
      uint index = g_skydome_texture_indices[i];
      float weight = g_skydome_texture_weights[i];
      if (index == 0xFFFFFFFFu) {
        sky += weight * float3(1, 0.41, 0.7);
      } else {
        if (weight == 0.f) continue;  // skip unused layers (filter is 25 taps)
        sky += t_skydome_texture_array.SampleGrad(s_skydome_texture_array_sampler_s, float3(uv, (float)index), dx, dy).xyz * weight;
      }
    }
  } else {
    sky = t_xml_base_colour.SampleGrad(s_xml_sampler_s, uv, dx, dy).xyz;
  }
  return sky;
}

float2 SkyTextureSize()
{
  uint width, height, elements;
  [branch]
  if (g_skydome_texture_array_enabled) {
    t_skydome_texture_array.GetDimensions(width, height, elements);
  } else {
    t_xml_base_colour.GetDimensions(width, height);
  }
  return float2(max(width, 1u), max(height, 1u));
}

// Threshold filter (Pharaoh's): 3 rings x 8 directions at 16/32/64 texels.
// Taps too different from the centre are rejected, so clouds/sun edges stay sharp.
float3 SkyDeband(float2 uv, float2 dx, float2 dy, float threshold)
{
  static const float2 directions[8] = {
    float2( 1.0,  0.0), float2(-1.0,  0.0),
    float2( 0.0,  1.0), float2( 0.0, -1.0),
    float2( 0.70710678,  0.70710678), float2(-0.70710678, -0.70710678),
    float2( 0.70710678, -0.70710678), float2(-0.70710678,  0.70710678)
  };

  float2 radius_uv = SKY_DEBAND_RADIUS_TEXELS / SkyTextureSize();
  float3 center = SkySampleGrad(uv, dx, dy);
  // Relative to the brightest channel: scale-free for the HDR texture.
  float center_max = max(max(center.r, center.g), max(center.b, 1e-6));

  float3 sum = center;
  float total_weight = 1.f;
  [loop]
  for (int tap = 0; tap < 24; ++tap) {
    float ring_scale = (tap < 8) ? 0.25 : ((tap < 16) ? 0.5 : 1.0);
    float2 tap_uv = uv + directions[tap & 7] * (radius_uv * ring_scale);
    tap_uv.y = saturate(tap_uv.y);  // stock clamps v
    float3 tap_rgb = SkySampleGrad(tap_uv, dx, dy);
    float3 difference = abs(tap_rgb - center);
    float error = max(difference.r, max(difference.g, difference.b)) / center_max;
    float weight = 1.f - smoothstep(0.5f * threshold, threshold, error);
    sum += tap_rgb * weight;
    total_weight += weight;
  }
  return sum / total_weight;
}

// Interleaved gradient noise, static in screen space. Relative, so HDR-safe.
float3 SkyDither(float3 color, float2 pixel)
{
  float noise = frac(52.9829189 * frac(dot(floor(pixel), float2(0.06711056, 0.00583715)))) - 0.5;
  return color * (1.f + noise * SKY_DITHER_AMOUNT);
}


void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float3 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  float4 v9 : TEXCOORD8,
  nointerpolation uint2 v10 : TEXCOORD9,
  nointerpolation float w10 : TEXCOORD10,
  nointerpolation uint x10 : NORMAL1,
  float3 v11 : TEXCOORD11,
  float4 v12 : COLOR1,
  float4 v13 : COLOR2,
  float4 v14 : NORMAL0,
  uint v15 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  // Flow direction from the vertex frame, rotated into wind space.
  float4 n = v13.zyyx * 2 - 1;
  float2 t = float2(n.z * v4.z, n.w * v4.y);
  float2 c = float2(v4.y * n.x - t.x, v4.x * n.y - t.y);
  float2 wind = normalize(g_wind_direction.xy);
  float2 flow_raw = float2(dot(c, wind), dot(wind.yx, n.xw)) * 0.5 + 0.5;

  // Per-material flow parameters.
  uint param_index = v10.x + g_material_offset;
  AUTO_PARAMETERS params = xml_auto_array_parameters[param_index];

  // Flow fades out towards the horizon and the pole.
  float horizon_mask = saturate(((1 - v3.y) - params.sp_horizon_flow_start) * (1 / params.sp_horizon_flow_fade_width));
  float pole_mask = saturate((1 / params.sp_pole_flow_mask_width) * v3.y);
  float flow_mask = min(horizon_mask, pole_mask);
  float2 flow_uv = flow_mask * (0.5 - flow_raw) + 0.5;
  float2 flow = params.sp_flow_power * float2(1 - 2 * flow_uv.x, 2 * flow_uv.y - 1);

  // Sky rotates with the sun's azimuth.
  float2 sun_xz = normalize(float2(sun_direction.x, sun_direction.z));
  float azimuth = atan2(sun_xz.x, sun_xz.y) * 0.159235656;  // 1/6.28 (stock)
  float2 base_uv = v3.xy + float2(azimuth, 0);

  // Two flow phases half a cycle apart, crossfaded.
  float phase_a = frac(params.sp_flow_speed * time_in_sec);
  float phase_b = frac(params.sp_flow_speed * time_in_sec + 0.5);
  float2 uv_a = flow * phase_a + base_uv;
  float2 uv_b = flow * phase_b + base_uv;
  float blend = (0.5 - phase_a) * 2;
  uv_a.y = saturate(uv_a.y);
  uv_b.y = saturate(uv_b.y);

  float2 dx_a = ddx(uv_a), dy_a = ddy(uv_a);
  float2 dx_b = ddx(uv_b), dy_b = ddy(uv_b);

  float3 sky_a, sky_b;
  bool deband = (HDR >= 0.5f) && (SI.sky_deband >= 0.5f);
  [branch]
  if (deband) {
    float threshold = (SI.sky_deband >= 2.5f) ? SKY_DEBAND_THRESHOLD_HIGH
                    : ((SI.sky_deband >= 1.5f) ? SKY_DEBAND_THRESHOLD_MEDIUM
                                               : SKY_DEBAND_THRESHOLD_LOW);
    sky_a = SkyDeband(uv_a, dx_a, dy_a, threshold);
    sky_b = SkyDeband(uv_b, dx_b, dy_b, threshold);
  } else {
    sky_a = SkySampleStock(uv_a);
    sky_b = SkySampleStock(uv_b);
  }

  float3 sky = abs(blend) * (sky_b - sky_a) + sky_a;

  // Pole tint.
  float pole_lerp = params.sp_pole_tint_amount * (v13.w - 1) + 1;
  sky = pole_lerp * (sky - params.sp_pole_colour) + params.sp_pole_colour;

  if (deband) {
    sky = SkyDither(sky, v0.xy);
  }

  o0.xyz = sky * (g_cubemap_sun_colour_scale * 7.61524207e-05);
  o0.w = 1;
  return;
}
