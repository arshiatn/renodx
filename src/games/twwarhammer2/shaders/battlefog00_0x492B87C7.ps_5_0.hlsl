// battlefog inscattering (492B87C7, the only fog light shader of the data run's traces, at the render size): the game's,
// its sun-shadow march with the rays' visibility in place of the shadow maps over the camera ray's first 400 m (Fog
// Shadows: twattila/rt_scene.hpp InstallFogShadows, twattila/rt_fog.hlsli). The game's lit share of the march scales
// the sun's and sky's light in the fog as before; the torches' and fires' light there comes apart (unshadowed by the
// sun). Runs only with t43 bound (addon.cpp FogRays); otherwise the game's own bytes do.
#define RT_FOG_SHADE 0.f
#include "../../twattila/rt_fog.hlsli"

cbuffer camera : register(b0)
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
  float2 g_viewport_origin : packoffset(c30);
  float4 g_render_target_dimensions : packoffset(c31);
  float4 g_camera_temp0 : packoffset(c32);
  float4 g_camera_temp1 : packoffset(c33);
  float4 g_camera_temp2 : packoffset(c34);
  float4 g_clip_rect : packoffset(c35);
  float3 g_vr_head_rotation : packoffset(c36);
  int g_num_of_samples : packoffset(c36.w);
  float g_supersampling : packoffset(c37);
  float4 g_mouse_position : packoffset(c38);
  float3 g_frustum_points[8] : packoffset(c39);
  float g_orthographic : packoffset(c46.w);
  float g_overlay_lerp : packoffset(c47);
  float g_overlay_parchment_lerp : packoffset(c47.y);
}

cbuffer shadowmap_PS : register(b1)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  float4 g_vSoftShadowBufferSize : packoffset(c1);
  float4x4 g_amHardSplit[4] : packoffset(c2);
  float4x4 g_amHardSplit_prewarp[4] : packoffset(c18);
  float2 g_fFadeRange : packoffset(c34);
  float g_sample_bias : packoffset(c34.z);
  float g_iSplitCount : packoffset(c34.w);
  float3 g_shadow_light_direction : packoffset(c35);
  float g_split_distances[5] : packoffset(c36);
  float4 g_split_uv_texture_bounds[4] : packoffset(c41);
}

cbuffer lighting_VS_PS : register(b2)
{
  bool g_apply_environment_specular : packoffset(c0);
  float3 sun_direction : packoffset(c0.y);
  float3 sun_colour : packoffset(c1);
  float3 ambient_cube_lr[2] : packoffset(c2);
  float3 ambient_cube_tb[2] : packoffset(c4);
  float3 ambient_cube_fb[2] : packoffset(c6);
  float3 g_deep_water_colour : packoffset(c8);
  float3 g_shallow_water_colour : packoffset(c9);
  float3 g_sea_bed_light_scatter : packoffset(c10);
  float g_refraction_light_scatter : packoffset(c10.w);
  int g_ssr_enabled : packoffset(c11);
  int g_use_spherical_harmonics : packoffset(c11.y);
  float2 g_cloud_shadow_direction : packoffset(c11.z);
  float g_cloud_shadow_speed : packoffset(c12);
  float g_cloud_shadow_scale : packoffset(c12.y);
  float g_cloud_shadow_lerp : packoffset(c12.z);
  float2 g_noise_uv_shift : packoffset(c13);
  float2 g_skin_curvature_scale_bias : packoffset(c13.z);
  float2 g_skin_translucency_scale_bias : packoffset(c14);
  float3 g_skin_blood_colour : packoffset(c15);
  float4 g_world_bounds : packoffset(c16);
  float4 g_playable_area_bounds : packoffset(c17);
  float g_vegetation_wrap_lighting_bias : packoffset(c18);
  float g_spherical_harmonic_terms : packoffset(c18.y);
  float g_spherical_harmonic_fadeout : packoffset(c18.z);
}

cbuffer fog_legacy : register(b3)
{
  float3 g_legacy_volume_fog_colour : packoffset(c0);
  float g_legacy_fog_distance_start : packoffset(c0.w);
  float g_legacy_fog_distance_strength : packoffset(c1);
  float g_legacy_fog_distance_scale : packoffset(c1.y);
  float g_legacy_fog_height_bottom : packoffset(c1.z);
  float g_legacy_fog_height_top : packoffset(c1.w);
  float g_legacy_fog_height_strength : packoffset(c2);
  float g_legacy_fog_colour_blend : packoffset(c2.y);
  float g_legacy_fog_clear_distance : packoffset(c2.z);
  float2 g_legacy_force_fog : packoffset(c3);
  float g_legacy_fog_lerp : packoffset(c3.z);
}

cbuffer fog : register(b4)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float g_fog_density_mie : packoffset(c1);
  float g_fog_noise : packoffset(c1.y);
  float3 g_fog_colour : packoffset(c2);
  uint g_fog_mode : packoffset(c2.w);
  float g_fog_lerp : packoffset(c3);
  float g_fog_clear_distance : packoffset(c3.y);
  float g_fog_y_multiplier : packoffset(c3.z);
  float g_campaign_density_scaling : packoffset(c3.w);
  float g_height_fog_clear_distance : packoffset(c4);
  float g_clear_distance_fade_out : packoffset(c4.y);
}

SamplerState s_volume_noise_s : register(s0);
SamplerState s_2d_noise_s : register(s1);
Texture2D<float4> g_tShadowBuffer_SM50 : register(t0);
Texture2D<float4> t_world_spherical_harmonics : register(t1);
Texture3D<float4> t_volume_noise : register(t2);
Texture2D<float4> t_2d_noise : register(t3);
Texture2D<float4> gbuffer_channel_4_texture : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0, 0, 1.000000, 0},
                              { 1, 0, 0, 0},
                              { 2, 0, 1.000000, 0},
                              { 0, 1, 0, 0},
                              { 1, 1, 0, 0},
                              { 2, 1, 0, 0},
                              { 0, 2, 0, 0},
                              { 1, 2, -1.000000, 0},
                              { 2, 2, -1.000000, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;

  uint rt_fog_width, rt_fog_height;
  t_rt_fog.GetDimensions(rt_fog_width, rt_fog_height);
  const bool rt_fog_bound = rt_fog_width != 0u;
  const float rt_fog_sun = RtFogSun(v0.xy);
  // The march starts at the clear distance: a step's distance from the camera is it + the step's.
  const float rt_fog_start = min(g_height_fog_clear_distance, g_fog_clear_distance);
  float rt_fog_lit = 16.f;  // the march's lit steps (its 16 the game's when it skips the march)

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.xy = g_screen_size.zw * r0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.zw = uiDest.xy;
  r1.xy = (uint2)r0.zw;
  r1.xy = r1.xy * r0.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.zw = (int2)r0.zw + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.zw);
  r1.zw = float2(0,0);
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r0.xy = g_render_target_dimensions.xy * r0.xy;
  r0.xy = g_viewport_dimensions.zw * r0.xy;
  r1.xy = r0.xy * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r0.www;
  r1.xy = float2(0.00200000009,0.00200000009) * r0.xz;
  r0.w = t_2d_noise.Sample(s_2d_noise_s, r1.xy).x;
  r0.w = g_legacy_force_fog.x * r0.w;
  r1.xyz = -camera_position.xyz + r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r2.x = sqrt(r1.w);
  r2.y = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r0.w = r0.w * 0.5 + r2.x;
  r2.z = g_legacy_force_fog.y + -g_legacy_force_fog.x;
  r0.w = -g_legacy_force_fog.x + r0.w;
  r2.z = 1 / r2.z;
  r0.w = saturate(r2.z * r0.w);
  r2.z = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r2.z * r0.w;
  r3.xyz = r1.xyz / r2.xxx;
  r2.z = dot(-r3.xyz, sun_direction.xyz);  // the decompile named c0.x (cb2[0].yzw)
  r2.w = 9.99999975e-005 + r2.y;
  r2.w = cmp(r2.w < r2.x);
  if (r2.w != 0) {
    r2.w = rsqrt(r1.w);
    r4.xyz = r2.www * r1.xyz;
    r4.xyz = r4.xyz * r2.yyy + camera_position.xyz;
    r5.xyz = -r4.xyz + r0.xyz;
    r0.y = dot(r5.xyz, r5.xyz);
    r0.y = sqrt(r0.y);
    r2.yw = r4.xz * float2(0.00100000005,0.00100000005) + v0.xy;
    r2.yw = r3.xy * float2(0.100000001,0.100000001) + r2.yw;
    r2.yw = float2(0.103100002,0.103100002) * r2.yw;
    r2.yw = frac(r2.yw);
    r5.xyz = float3(19.1900005,19.1900005,19.1900005) + r2.wyy;
    r1.y = dot(r2.ywy, r5.xyz);
    r2.yw = r2.yw + r1.yy;
    r1.y = r2.y + r2.w;
    r1.y = r1.y * r2.y;
    r1.y = frac(r1.y);
    r1.y = frac(r1.y);
    r2.y = -5000 + r0.y;
    r2.y = saturate(0.000125000006 * r2.y);
    r2.w = r2.y * -2 + 3;
    r2.y = r2.y * r2.y;
    r2.y = r2.w * r2.y;
    r2.w = 0.25 + -r1.y;
    r1.y = r2.y * r2.w + r1.y;
    r2.yw = float2(0.0625,0.03125) * r0.yy;
    r3.w = cmp(0 < g_fog_noise);
    r5.w = 1;
    r6.w = 1;
    r7.zw = float2(0,0);
    r10.xy = float2(0,0);
    r4.w = 0;
    r8.xyzw = float4(0,0,0,0);
    r9.xyzw = float4(0,0,0,0);
    rt_fog_lit = 0.f;
    [loop] while (true) {  // a loop as the game's (fxc unrolls it for the noise's gradients)
      r10.z = cmp((int)r9.w >= 16);
      if (r10.z != 0) break;
      r10.z = (int)r9.w;
      r10.z = r10.z + r1.y;
      r10.z = r2.y * r10.z + r2.w;
      r10.z = min(r10.z, r0.y);
      r5.xyz = r3.xyz * r10.zzz + r4.xyz;
      r10.w = dot(r5.xyzw, view_projection._m03_m13_m23_m33);
      r11.xy = cmp(r10.zz < float2(1000,400));
      const bool rt_fog_near = rt_fog_bound && rt_fog_start + r10.z < 400.f;
      r10.z = cmp(1 < (int)r9.w);
      r10.z = r10.z ? r11.x : 0;
      r6.xyz = -g_shadow_light_direction.xyz * g_sample_bias + r5.xyz;
      r12.x = cmp(r10.w >= g_split_distances[1]);
      r12.y = cmp(r10.w >= g_split_distances[2]);
      r12.z = cmp(r10.w >= g_split_distances[3]);
      r12.w = cmp(r10.w >= g_split_distances[4]);
      r12.xyzw = r12.xyzw ? float4(1,1,1,1) : 0;
      r10.w = dot(r12.xyzw, r12.xyzw);
      r10.w = 0.5 + r10.w;
      r10.w = floor(r10.w);
      r11.x = (int)r10.w;
      r11.z = (uint)r11.x << 2;
      r12.x = dot(r6.xyzw, g_amHardSplit[r11.z/4]._m00_m10_m20_m30);
      r12.y = dot(r6.xyzw, g_amHardSplit[r11.z/4]._m01_m11_m21_m31);
      r12.z = dot(r6.xyzw, g_amHardSplit[r11.z/4]._m02_m12_m22_m32);
      r6.x = dot(r6.xyzw, g_amHardSplit[r11.z/4]._m03_m13_m23_m33);
      r13.xyz = r12.xyz / r6.xxx;
      r6.y = cmp(r10.w >= g_iSplitCount);
      r11.zw = cmp(r13.xy < g_split_uv_texture_bounds[r11.x].xy);
      r6.z = (int)r11.w | (int)r11.z;
      r11.xz = cmp(g_split_uv_texture_bounds[r11.x].zw < r13.xy);
      r10.w = (int)r11.z | (int)r11.x;
      r6.z = (int)r6.z | (int)r10.w;
      r6.y = (int)r6.z | (int)r6.y;
      if (r6.y == 0 && !rt_fog_near) {
        r6.xy = r12.xy / r6.xx;
        r6.xy = g_vHardShadowBufferSize.xy * r6.xy;
        r6.xy = floor(r6.xy);
        r7.xy = (int2)r6.xy;
        r6.x = g_tShadowBuffer_SM50.Load(r7.xyz).x;
        r6.x = cmp(r13.z < r6.x);
        r6.x = r6.x ? 1.000000 : 0;
      } else {
        r6.x = 1;
      }
      r6.x = r10.z ? r6.x : 1;
      // With the rays: the step's light counted apart (the rays' near, the maps' further on), the fog's weights open.
      if (rt_fog_bound) {
        rt_fog_lit += rt_fog_near ? rt_fog_sun : r6.x;
        r6.x = 1;
      }
      r6.y = r3.w ? r11.y : 0;
      if (r6.y != 0) {
        r11.xyz = time_in_sec * float3(4.4000001,-4.4000001,4.4000001) + r5.xyz;
        r11.xyz = float3(0.00999999978,0.00999999978,0.00999999978) * r11.xyz;
        r5.x = t_volume_noise.Sample(s_volume_noise_s, r11.xyz).x;
        r5.x = r5.x * 2 + -1;
        r5.x = r5.x * g_fog_noise + 1;
      } else {
        r5.x = 1;
      }
      r8.w = r8.w + r5.x;
      r5.x = 60000 + -r5.y;
      r5.z = r5.x * 0.5 + r5.y;
      r5.yz = max(float2(0,0), r5.yz);
      r6.yz = float2(-0.000206276105,-0.00120224594) * r5.zz;
      r6.yz = exp2(r6.yz);
      r5.z = g_fog_density_mie * r6.z;
      r7.xy = float2(-0.000206276105,-0.00120224594) * r5.yy;
      r7.xy = exp2(r7.xy);
      r5.y = r7.x * r2.y;
      r10.x = r7.x * r2.y + r10.x;
      r6.z = g_fog_density_mie * r7.y;
      r7.x = r6.z * r2.y;
      r10.y = r6.z * r2.y + r10.y;
      r6.y = r6.y * r5.x + r10.x;
      r5.x = r5.z * r5.x + r10.y;
      r5.x = 2.20000002e-005 * r5.x;
      r11.xyz = r6.yyy * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r5.xxx;
      r11.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r11.xyz;
      r11.xyz = exp2(r11.xyz);
      r5.xyz = r11.xyz * r5.yyy;
      r8.xyz = r5.xyz * r6.xxx + r8.xyz;
      r5.xyz = r11.xyz * r7.xxx;
      r9.xyz = r5.xyz * r6.xxx + r9.xyz;
      r4.w = r6.x + r4.w;
      r9.w = (int)r9.w + 1;
    }
    r0.y = r2.z * r2.z + 1;
    r1.y = 0.5 * r0.y;
    r3.xzw = r8.xyz * r1.yyy;
    r1.y = -r2.z * 1.51999998 + 1.5776;
    r1.y = log2(abs(r1.y));
    r1.y = 1.5 * r1.y;
    r1.y = exp2(r1.y);
    r0.y = r0.y / r1.y;
    r0.y = 0.000978047145 * r0.y;
    r4.xyz = r9.xyz * r0.yyy;
    r4.xyz = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r4.xyz;
    r3.xzw = r3.xzw * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r4.xyz;
    r0.y = 0.0625 * r4.w;
    r1.y = 0.0625 * r8.w;
    r2.y = -g_height_fog_clear_distance + r2.x;
    r2.y = max(0, r2.y);
    r2.w = r3.y * g_height_fog_clear_distance + camera_position.y;
    r4.x = cmp(r2.w >= g_fog_height_top);
    r4.y = cmp(0 < r3.y);
    r4.x = r4.y ? r4.x : 0;
    if (r4.x == 0) {
      r4.xy = -float2(g_fog_height_top, g_fog_height_bottom) + r2.ww;  // the decompile wrote cb4[0].wz as one
      r4.z = 10000 + r2.w;
      r4.xyz = r4.xyz / -r3.yyy;
      r4.w = min(r4.x, r4.y);
      r4.x = max(r4.x, r4.y);
      r5.x = min(r4.y, r4.z);
      r4.y = max(r4.y, r4.z);
      r4.z = max(0, r5.x);
      r4.xyw = max(float3(0,0,0), r4.xyw);
      r5.x = g_fog_density_height * r1.y;
      r5.x = 0.0125000002 * r5.x;
      r5.y = min(r4.w, r4.x);
      r5.z = max(r4.w, r2.y);
      r5.z = min(r5.z, r4.x);
      r5.w = cmp(r5.y != r5.z);
      r2.w = r4.w * r3.y + r2.w;
      r5.z = r5.z + -r4.w;
      r6.x = r5.y + -r4.w;
      r6.y = g_fog_height_top + -g_fog_height_bottom;
      r6.y = r6.y + r6.y;
      r2.w = r2.w + r2.w;
      r6.z = r6.x * r3.y + r2.w;
      r6.z = -g_fog_height_top * 2 + r6.z;
      r6.x = r6.x * r6.z;
      r6.x = r6.x * r5.x;
      r6.x = r6.x / r6.y;
      r6.x = 1.44269502 * r6.x;
      r6.x = exp2(r6.x);
      r6.z = r5.z * r3.y + r2.w;
      r6.z = -g_fog_height_top * 2 + r6.z;
      r5.z = r6.z * r5.z;
      r5.z = r5.z * r5.x;
      r5.z = r5.z / r6.y;
      r5.z = 1.44269502 * r5.z;
      r5.z = exp2(r5.z);
      r5.z = r6.x + -r5.z;
      r6.x = min(r4.z, r4.y);
      r6.z = max(r5.y, r4.z);
      r6.z = min(r6.z, r4.y);
      r6.w = cmp(r6.x != r6.z);
      r6.z = r6.z + -r4.z;
      r6.z = -r6.z * r5.x;
      r6.z = 1.44269502 * r6.z;
      r6.z = exp2(r6.z);
      r6.z = r6.z * r5.z;
      r5.z = r6.w ? r6.z : r5.z;
      r5.z = r5.w ? r5.z : 0;
      r5.w = max(r4.z, r2.y);
      r4.y = min(r5.w, r4.y);
      r5.w = cmp(r6.x != r4.y);
      r6.z = r4.y + -r4.z;
      r4.z = r6.x + -r4.z;
      r4.z = -r4.z * r5.x;
      r4.z = 1.44269502 * r4.z;
      r4.z = exp2(r4.z);
      r6.x = -r6.z * r5.x;
      r6.x = 1.44269502 * r6.x;
      r6.x = exp2(r6.x);
      r4.z = -r6.x + r4.z;
      r4.y = max(r4.y, r4.w);
      r4.x = min(r4.y, r4.x);
      r4.y = cmp(r5.y != r4.x);
      r4.x = r4.x + -r4.w;
      r2.w = r4.x * r3.y + r2.w;
      r2.w = -g_fog_height_top * 2 + r2.w;
      r2.w = r4.x * r2.w;
      r2.w = r2.w * r5.x;
      r2.w = r2.w / r6.y;
      r2.w = 1.44269502 * r2.w;
      r2.w = exp2(r2.w);
      r2.w = r4.z * r2.w;
      r2.w = r4.y ? r2.w : r4.z;
      r2.w = r5.z + r2.w;
      r4.xyz = r5.www ? r2.www : r5.zzz;
    } else {
      r4.xyz = float3(0,0,0);
    }
    r4.xyz = r4.xyz * r0.yyy;
    r2.w = 1 / g_clear_distance_fade_out;
    r2.y = saturate(r2.y * r2.w);
    r2.w = r2.y * -2 + 3;
    r2.y = r2.y * r2.y;
    r2.y = r2.w * r2.y;
    r4.xyz = r4.xyz * r2.yyy;
    r2.x = -g_fog_clear_distance + r2.x;
    r2.x = max(0, r2.x);
    r2.y = cmp(0.00999999978 < g_clear_distance_fade_out);
    r2.w = cmp(0.00999999978 < r2.x);
    r2.y = r2.w ? r2.y : 0;
    r2.w = min(g_clear_distance_fade_out, r2.x);
    r3.y = r2.w * r2.w;
    r4.w = g_clear_distance_fade_out + g_clear_distance_fade_out;
    r3.y = r3.y / r4.w;
    r2.w = r2.x + -r2.w;
    r2.w = r3.y + r2.w;
    r2.w = r2.w / r2.x;
    r2.y = r2.y ? r2.w : 1;
    r1.y = g_fog_density_constant * r1.y;
    r1.y = r1.y * r2.y;
    r1.y = r1.y * r2.x;
    r1.y = 0.0180336889 * r1.y;
    r2.x = exp2(r1.y);
    r1.y = exp2(-r1.y);
    r2.y = r2.x + r1.y;
    r1.y = r2.x + -r1.y;
    r1.y = 0.5 * r1.y;
    r1.y = saturate(r2.y * 0.5 + -r1.y);
    r1.y = 1 + -r1.y;
    r0.y = r1.y * r0.y;
    r2.xyw = max(r4.xyz, r0.yyy);
  } else {
    r2.xyw = float3(0,0,0);
    r3.xzw = float3(0,0,0);
  }
  r2.xyw = max(r2.xyw, r0.www);
  r0.y = r2.z * r2.z + 1;
  r0.y = 0.5 * r0.y;
  r2.xyz = r2.xyw * r0.yyy + r3.xzw;
  r2.xyz = g_fog_colour.xyz * r2.xyz;
  if (g_use_spherical_harmonics != 0) {
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r0.yw = uiDest.xy;
    r3.xyzw = g_playable_area_bounds.xyxy + g_playable_area_bounds.zwzw;
    r4.xyzw = float4(0.5,0.5,0.5,0.5) * r3.xyzw;
    r3.xyzw = -r3.zwzw * float4(0.5,0.5,0.5,0.5) + g_playable_area_bounds.xyzw;
    r3.xyzw = r3.xyzw * float4(1.04999995,1.04999995,1.04999995,1.04999995) + r4.xyzw;
    r4.xy = cmp(r0.xz < r3.xy);
    r4.zw = cmp(r3.zw < r0.xz);
    r4.xy = (int2)r4.zw | (int2)r4.xy;
    r1.y = (int)r4.y | (int)r4.x;
    r1.w = rsqrt(r1.w);
    r1.xz = r1.xz * r1.ww;
    r4.xy = cmp(r1.xz < float2(0,0));
    r3.xy = camera_position.xz + -r3.xy;
    r3.zw = -camera_position.xz + r3.zw;
    r3.xy = r4.xy ? r3.xy : r3.zw;
    r3.zw = float2(9.99999975e-005,9.99999975e-005) + abs(r1.xz);
    r3.xy = r3.xy / r3.zw;
    r1.w = min(r3.x, r3.y);
    r1.xz = r1.xz * r1.ww + camera_position.xz;
    r0.xz = r1.yy ? r1.xz : r0.xz;
    r0.xz = -g_playable_area_bounds.xy + r0.xz;
    r1.xy = g_playable_area_bounds.zw + -g_playable_area_bounds.xy;
    r0.xz = r0.xz / r1.xy;
    r1.xy = float2(-1,-1) + r0.xz;
    r1.xy = max(r1.xy, -r0.xz);
    r1.xy = max(float2(0,0), r1.xy);
    r1.x = max(r1.x, r1.y);
    r0.xz = saturate(r0.xz);
    r0.xz = frac(r0.xz);
    uiDest.yw = (uint2)r0.yw / uint2(3,3);  // the decompile lost the divisor (udiv by 3: the 3 x 3 tiles)
    r0.yw = uiDest.yw;
    r0.yw = (uint2)r0.yw;
    r1.yz = r0.yw * r0.xz;
    r3.xy = trunc(r1.yz);
    r1.yz = frac(r1.yz);
    r3.zw = float2(1,1) / r0.yw;
    r0.xz = r3.zw + r0.xz;
    r0.xz = frac(r0.xz);
    r0.xy = r0.yw * r0.xz;
    r0.xy = trunc(r0.xy);
    r0.z = round(g_spherical_harmonic_terms);
    r0.z = (int)r0.z;
    r4.zw = float2(0,0);
    r5.zw = float2(0,0);
    r6.zw = float2(0,0);
    r7.zw = float2(0,0);
    r8.xyz = float3(0,0,0);
    r0.w = 0;
    while (true) {
      r1.w = cmp((int)r0.w >= (int)r0.z);
      if (r1.w != 0) break;
      r1.w = (uint)r0.w % 3;
      r1.w = (int)r1.w;
      r2.w = r3.x * 3 + r1.w;
      r3.z = (int)icb[r0.w+0].y;
      r3.w = r3.y * 3 + r3.z;
      r4.x = (int)r2.w;
      r4.y = (int)r3.w;
      r9.xyz = t_world_spherical_harmonics.Load(r4.xyz).xyz;
      r1.w = r0.x * 3 + r1.w;
      r5.x = (int)r1.w;
      r5.y = (int)r3.w;
      r10.xyz = t_world_spherical_harmonics.Load(r5.xyz).xyz;
      r3.z = r0.y * 3 + r3.z;
      r6.x = (int)r2.w;
      r6.y = (int)r3.z;
      r11.xyz = t_world_spherical_harmonics.Load(r6.xyz).xyz;
      r7.x = (int)r1.w;
      r7.y = (int)r3.z;
      r12.xyz = t_world_spherical_harmonics.Load(r7.xyz).xyz;
      r10.xyz = r10.xyz + -r9.xyz;
      r9.xyz = r1.yyy * r10.xyz + r9.xyz;
      r10.xyz = r12.xyz + -r11.xyz;
      r10.xyz = r1.yyy * r10.xyz + r11.xyz;
      r10.xyz = r10.xyz + -r9.xyz;
      r9.xyz = r1.zzz * r10.xyz + r9.xyz;
      r8.xyz = r9.xyz * icb[r0.w+0].zzz + r8.xyz;
      r0.w = (int)r0.w + 1;
    }
    r0.x = saturate(g_spherical_harmonic_fadeout * r1.x);
    r0.y = cmp(0 < r0.x);
    r1.xyz = ambient_cube_tb[0].xyz + -r8.xyz;
    r0.xzw = r0.xxx * r1.xyz + r8.xyz;
    r0.xyz = r0.yyy ? r0.xzw : r8.xyz;
  } else {
    r0.xyz = ambient_cube_tb[0].xyz;
  }
  r0.xyz = sun_colour.xyz * float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) + r0.xyz;
  if (rt_fog_bound) r0.xyz = r0.xyz * (0.0625 * rt_fog_lit) + RtFogLights(v0.xy);
  r0.xyz = r2.xyz * r0.xyz;
  o0.xyz = g_fog_lerp * r0.xyz;
  o0.w = g_fog_lerp;
  return;
}
