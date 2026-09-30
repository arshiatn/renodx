#include "../shared.h"

// Total War: Pharaoh -- sky debanding for 0x9CF980D0.
// Intended target: main / ps_5_0. Not compiled or tested in the game here.
// SI.sky_deband == 0: stock sampling and grading.
// SI.sky_deband == 1: low, with a 6-code filter threshold.
// SI.sky_deband == 2: medium, with an 11-code filter threshold.
// SI.sky_deband == 3: high, with a 20-code filter threshold.
// All enabled presets use the same 24-neighbor filter and dithering strength.
// The game's alpha and color grading are preserved in every mode.
// Requires the BC7_UNORM_SRGB t0 view shown in the supplied capture.
//
// Sample three rings at 16, 32, and 64 base-level texels.
// Higher thresholds admit larger color differences when smoothing the source.
// Enabled filtering uses 25 texture samples including the original center.
// Dither is 4.0 sRGB code values peak-to-peak (approximately +/-2 of an 8-bit
// sRGB code value) for all three enabled presets.
// Noise is static in screen space and applied before the game's HDR scaling.

static const float SKY_DEBAND_RADIUS_TEXELS = 64.0;
static const float SKY_DEBAND_THRESHOLD_LOW_CODES = 6.0;
static const float SKY_DEBAND_THRESHOLD_MEDIUM_CODES = 11.0;
static const float SKY_DEBAND_THRESHOLD_HIGH_CODES = 20.0;
static const float SKY_DITHER_CODES = 4.0;

// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 22 04:29:05 2026

cbuffer environment_PS : register(b0)
{
  bool g_apply_cubemap_interpolation : packoffset(c0);
  float g_skydome_lerp : packoffset(c0.y);
  float4 g_skydome_params : packoffset(c1);
  float4 g_skydome_shading : packoffset(c2);
  float g_environment_weights[8] : packoffset(c3);
  float g_environment_indices[8] : packoffset(c11);
  float g_environment_count : packoffset(c18.y);
  float g_environment_scale : packoffset(c18.z);
  float g_porthole_env_scale : packoffset(c18.w);
  float g_porthole_pp_saturation : packoffset(c19);
  float g_porthole_pp_contrast : packoffset(c19.y);
}

cbuffer lighting_VS_PS : register(b1)
{
  bool g_apply_environment_specular : packoffset(c0);
  bool g_contact_shadows : packoffset(c0.y);
  float3 sun_direction : packoffset(c1);
  float3 sun_disk_direction : packoffset(c2);
  float3 sun_colour : packoffset(c3);
  float3 sun_colour_unscaled : packoffset(c4);
  float2 sun_angular_radius : packoffset(c5);
  float sky_colour_scale : packoffset(c5.z);
  float3 ambient_cube_lr[2] : packoffset(c6);
  float3 ambient_cube_tb[2] : packoffset(c8);
  float3 ambient_cube_fb[2] : packoffset(c10);
  float3 g_deep_water_colour : packoffset(c12);
  float3 g_shallow_water_colour : packoffset(c13);
  float3 g_sea_bed_light_scatter : packoffset(c14);
  int g_ssr_enabled : packoffset(c14.w);
  float2 g_cloud_shadow_position : packoffset(c15);
  float g_cloud_shadow_scale : packoffset(c15.z);
  float2 g_cloud_shadow_lerp : packoffset(c16);
  float3 g_cloud_shadow_tiling_params : packoffset(c17);
  float2 g_noise_uv_shift : packoffset(c18);
  float2 g_skin_curvature_scale_bias : packoffset(c18.z);
  float2 g_skin_translucency_scale_bias : packoffset(c19);
  float3 g_skin_blood_colour : packoffset(c20);
  float4 g_world_bounds : packoffset(c21);
  float4 g_water_plane_bounds : packoffset(c22);
  float4 g_playable_area_bounds : packoffset(c23);
  float g_vegetation_wrap_lighting_bias : packoffset(c24);
  float g_spherical_harmonic_terms : packoffset(c24.y);
  float g_spherical_harmonic_fadeout : packoffset(c24.z);
  float g_debug_puddle_map : packoffset(c24.w);
  float g_debug_white_diffuse : packoffset(c25);
  float g_debug_light_diffuse_coef : packoffset(c25.y);
  float g_debug_light_ambient_coef : packoffset(c25.z);
  float g_debug_light_specular_coef : packoffset(c25.w);
  float g_debug_light_shadow_coef : packoffset(c26);
  float g_light_vegetation_backscattering_coef : packoffset(c26.y);
  float g_debug_light_bs_coef2 : packoffset(c26.z);
  float g_borders_colour_scale : packoffset(c26.w);
  float g_water_caustics_scale : packoffset(c27);
  float g_water_specular_scale : packoffset(c27.y);
  float g_campaign_water_attrition_strength : packoffset(c27.z);
  float g_water_light_scattering : packoffset(c27.w);
  float g_water_light_scattering_battle_rivers : packoffset(c28);
  float g_dynamic_light_scale : packoffset(c28.y);
  float4 g_skybox_cylinder_params : packoffset(c29);
  float4 g_fog_cell_shading : packoffset(c30);
  float2 g_aristeia_pos : packoffset(c31);
  float4 g_aristeia_effect : packoffset(c32);
  float g_campaign_flat_map : packoffset(c33);
  bool g_interactive_water_enabled : packoffset(c33.y);
  float4 g_interactive_water_bounds : packoffset(c34);
  float4 g_rain_camera_position : packoffset(c35);
  float4 g_rain_camera_mat_col1 : packoffset(c36);
  float4 g_rain_camera_mat_col2 : packoffset(c37);
  float4 g_rain_camera_mat_col3 : packoffset(c38);
  float4 g_rain_direction : packoffset(c39);
  float4 g_rain_color : packoffset(c40);
  float4 g_rain_lightning : packoffset(c41);
  float4 g_rain_puddles_params : packoffset(c42);
  float4 g_sky_correction_colour : packoffset(c43);
  float g_sky_correction_contrast : packoffset(c44);
  float g_sky_correction_brightness : packoffset(c44.y);
  float4 g_nile_colour : packoffset(c45);
  float g_nile_flow_speed : packoffset(c46);
  float4 g_puddle_map_bounds : packoffset(c47);
  int g_high_quality_puddle_sampling : packoffset(c48);
  float2 g_world_height_bounds : packoffset(c48.y);
  float4 g_screen_space_shadows_parameters : packoffset(c49);
  float g_generating_light_probe : packoffset(c50);
  int g_screen_space_shadows_step_count : packoffset(c50.y);
  int g_screen_space_environment_occlusion_quality : packoffset(c50.z);
  int g_screen_space_environment_occlusion_step_count : packoffset(c50.w);
  float g_screen_space_environment_occlusion_max_trace_distance : packoffset(c51);
  float2 g_screen_space_environment_smoothness_bounds : packoffset(c51.y);
  float2 g_nile_map_offset : packoffset(c52);
}

SamplerState s_diffuse_s : register(s0);
Texture2D<float4> t_diffuse : register(t0);


// 3Dmigoto declarations
#define cmp -



// The SRGB resource view already decodes t0 RGB to linear on sampling.
// Re-encoding here is ONLY for perceptual comparisons and noise amplitude;
// the filter averages in linear space and returns linear RGB to the game.
float3 SkyEncodeSRGB(float3 c)
{
  c = max(c, 0.0);
  float3 lo = 12.92 * c;
  float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
  return lerp(lo, hi, step(0.0031308, c));
}

float3 SkyDecodeSRGB(float3 c)
{
  c = max(c, 0.0);
  float3 lo = c / 12.92;
  float3 hi = pow((c + 0.055) / 1.055, 2.4);
  return lerp(lo, hi, step(0.04045, c));
}

float3 SkyDebandRGB(float2 uv, float3 center, float2 dx, float2 dy,
                   float thresholdCodes)
{
  uint width, height;
  t_diffuse.GetDimensions(width, height);
  float2 radiusUV = SKY_DEBAND_RADIUS_TEXELS / float2(width, height);

  // Use UV derivatives computed before the runtime branch for every tap.
  // Keep the original bound sampler and texture LOD behavior.
  float3 centerCode = SkyEncodeSRGB(center);
  float threshold = max(thresholdCodes / 255.0, 1e-6);

  // Eight symmetric directions at each of three radii. Nearby taps help
  // smooth narrow bands; wider taps can reach across broader plateaus.
  // Every tap is evaluated against the original center color to avoid
  // progressively admitting unrelated colors while accumulating samples.
  const float2 directions[8] = {
    float2( 1.0,  0.0), float2(-1.0,  0.0),
    float2( 0.0,  1.0), float2( 0.0, -1.0),
    float2( 0.70710678,  0.70710678),
    float2(-0.70710678, -0.70710678),
    float2( 0.70710678, -0.70710678),
    float2(-0.70710678,  0.70710678)
  };
  const float ringScales[3] = { 0.25, 0.5, 1.0 };

  float3 sum = center;
  float totalWeight = 1.0;
  [unroll]
  for (int ring = 0; ring < 3; ++ring)
  {
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
      float3 sampleRGB = t_diffuse.SampleGrad(
          s_diffuse_s,
          uv + directions[i] * (radiusUV * ringScales[ring]), dx, dy).rgb;
      float3 difference = abs(SkyEncodeSRGB(sampleRGB) - centerCode);
      float error = max(difference.r, max(difference.g, difference.b));
      // One weight for all channels avoids separate-channel edge decisions.
      // Full weight below half the threshold; reject above the full threshold.
      float weight = 1.0 - smoothstep(0.5 * threshold, threshold, error);
      sum += sampleRGB * weight;
      totalWeight += weight;
    }
  }
  return sum / totalWeight;
}

float3 SkyDitherRGB(float3 linearRGB, float2 pixel)
{
  // Interleaved gradient noise, fixed to screen pixels (no time uniform).
  float noise = frac(52.9829189 * frac(dot(
      floor(pixel), float2(0.06711056, 0.00583715)))) - 0.5;
  float3 code = SkyEncodeSRGB(linearRGB);
  code += noise * (SKY_DITHER_CODES / 255.0);
  // This clamp acts only on the UNORM source before any HDR scaling.
  // Never apply this clamp to the graded HDR output.
  return SkyDecodeSRGB(saturate(code));
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = -g_skydome_params.x + v2.x;
  r0.y = v2.y;
  float2 skyUV = r0.xy;
  float2 skyDx = ddx(skyUV);
  float2 skyDy = ddy(skyUV);
  r0.xyzw = t_diffuse.Sample(s_diffuse_s, r0.xy).xyzw;

  [branch]
  if (SI.sky_deband > 0.0)
  {
    float thresholdCodes = (SI.sky_deband >= 3.0)
        ? SKY_DEBAND_THRESHOLD_HIGH_CODES
        : ((SI.sky_deband >= 2.0)
            ? SKY_DEBAND_THRESHOLD_MEDIUM_CODES
            : SKY_DEBAND_THRESHOLD_LOW_CODES);
    r0.rgb = SkyDebandRGB(skyUV, r0.rgb, skyDx, skyDy, thresholdCodes);
    r0.rgb = SkyDitherRGB(r0.rgb, v0.xy);
  }

  r1.xyz = sky_colour_scale * r0.xyz;
  r1.w = dot(r1.xyz, float3(0.330000013,0.550000012,0.119999997));
  r0.xyz = -r0.xyz * sky_colour_scale + r1.www;
  o0.w = -r0.w * 0.100000001 + 1;
  r0.xyz = g_sky_correction_colour.www * r0.xyz + r1.xyz;
  r0.xyz = float3(-0.5,-0.5,-0.5) + r0.xyz;
  r0.w = 1 + g_sky_correction_contrast;
  r0.xyz = r0.xyz * r0.www + g_sky_correction_brightness;
  r0.xyz = float3(0.5,0.5,0.5) + r0.xyz;
  o0.xyz = g_sky_correction_colour.xyz * r0.xyz;
  return;
}
