// directionallight.hlsl ps30_overlay_highdef: the sun with the map overlay.
// standard_lighting_model_directional_light (lighting.hlsl): on foliage (IS_FOLIAGE: grass, leaves, flowers) the
// sun's specular makes white specks that flicker with DLSS's jitter facing the sun (ATN debug13; 0 removes them,
// v18). RenoDX: on IS_FOLIAGE pixels the sun's specular x Foliage Sun Glints (1: the game's).
// Render scale (DLSS addon): the map overlay (roads, borders, rivers; 4800x2700, drawn at its full size for the whole
// view) was read at its projected position x the viewport over the screen, the render size's part of it: below 100%
// roads moved with the camera and sat in the wrong place (ATN debug22). Read at the projected position (the viewport
// factor divided out; at full size the game's).
#include "../shared.h"
#include "./rt_sun.hlsli"

cbuffer camera_VS_PS : register(b0)
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
  float g_texel_offset : packoffset(c35.z);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float4 screen_size : packoffset(c37);
  float4 g_viewport_dimensions : packoffset(c38);
  float2 g_viewport_origin : packoffset(c39);
  float4 g_render_target_dimensions : packoffset(c40);
  float4 g_camera_temp0 : packoffset(c41);
  float2 g_inv_screen_size_minus_one : packoffset(c42);
  float2 g_camera_jitter : packoffset(c42.z);
  float2 g_prev_camera_jitter : packoffset(c43);
}

cbuffer environment_PS : register(b1)
{
  float3 g_sun_direction : packoffset(c0);
  float4 g_sky_exposure_clamp_threshold_falloff : packoffset(c1);
  float4 g_sky_gamma_etc : packoffset(c2);
  float4 g_probe_grid_area : packoffset(c3);
  uint g_probe_grid_size : packoffset(c4);
  float g_specular_probes_count : packoffset(c4.y);
  float g_specular_probe_lerp : packoffset(c4.z);
  float g_sky_fog_height_stretch : packoffset(c4.w);
  float g_sky_fog_height_offset : packoffset(c5);
  float g_sky_fog_gradient_exponent : packoffset(c5.y);
  float g_sky_fog_opacity : packoffset(c5.z);
  float g_spherical_harmonic_terms : packoffset(c5.w);
  float g_spherical_harmonic_fadeout : packoffset(c6);
}

cbuffer shroud_PS : register(b2)
{
  float g_shroud_enabled : packoffset(c0) = {0};
  float g_campaign_autumn_timer : packoffset(c0.y) = {0};
  float4 g_campaign_world_bounds : packoffset(c1) = {0,0,1,1};
  float4 g_campaign_playable_area_bounds : packoffset(c2) = {0,0,1,1};
  float g_snow_height : packoffset(c3) = {1.39999998};
  float g_rock_slope_scale : packoffset(c3.y) = {0.400000006};
  float g_rock_height_min : packoffset(c3.z) = {0.400000006};
  float g_rock_height_max : packoffset(c3.w) = {1.39999998};
}

cbuffer lighting_VS_PS : register(b3)
{
  float3 sun_direction : packoffset(c0);
  float3 sun_colour : packoffset(c1);
  float sun_specular : packoffset(c1.w);
  float g_cube_ambient_influence : packoffset(c2);
  float3 ambient_cube_lr[2] : packoffset(c3);
  float3 ambient_cube_tb[2] : packoffset(c5);
  float3 ambient_cube_fb[2] : packoffset(c7);
  float3 g_deep_water_colour : packoffset(c9);
  float3 g_shallow_water_colour : packoffset(c10);
  bool g_use_spherical_harmonics : packoffset(c10.w);
  float2 g_cloud_shadow_direction : packoffset(c11);
  float g_cloud_shadow_speed : packoffset(c11.z);
  float g_cloud_shadow_scale : packoffset(c11.w);
  float g_cloud_shadow_lerp : packoffset(c12);
  float4 g_playable_area_bounds : packoffset(c13);
  float g_vegetation_wrap_lighting_bias : packoffset(c14);
  float g_ambient_fudge_factor : packoffset(c14.y);
  float g_time_of_day_unary : packoffset(c14.z);
  float g_SSAO_low_clamp : packoffset(c14.w);
  float g_Shadow_low_clamp : packoffset(c15);
  float g_light_scale_for_units : packoffset(c15.y);
  float4 g_water_reflection_tint : packoffset(c16);
  float g_water_reflection_strength : packoffset(c17);
}

cbuffer directional_light_PS : register(b4)
{
  float3 light_direction : packoffset(c0);
  float3 light_colour : packoffset(c1);
  float4x4 g_overlay_view_projection : packoffset(c2);
  float g_refraction_depth : packoffset(c6);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_ibl_brdf_lut_s : register(s1);
SamplerState s_shroud_s : register(s2);
SamplerState s_ssao_s : register(s3);
SamplerState s_cloud_shadow_s : register(s4);
SamplerState s_overlay_s : register(s5);
Texture2D<float4> s_overlay : register(t0);
Texture2D<float4> s_shroud : register(t1);
TextureCubeArray<float4> t_environment_array : register(t2);
TextureCubeArray<float4> t_prev_environment_array : register(t3);
Texture2D<float4> t_world_spherical_harmonics : register(t4);
Texture2D<float4> t_prev_world_spherical_harmonics : register(t5);
Texture2D<float4> t_ibl_brdf_lut : register(t6);
Texture2D<float4> t_ssao : register(t7);
Texture2D<float4> t_cloud_shadow : register(t8);
Texture2D<float4> gbuffer_normal_texture : register(t9);
Texture2D<float4> gbuffer_colour_texture : register(t10);
Texture2D<float4> gbuffer_constants_texture : register(t11);
Texture2D<float4> gbuffer_readable_depth_texture : register(t12);
Texture2D<float4> t_shadow : register(t13);


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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21;
  uint4 bitmask, uiDest;
  float3 sun_tinted = sun_colour.xyz;  // Coloured Shadows: RtSunColour once the pixel is known
  float4 fDest;

  r0.xy = screen_size.zw * v0.xy;
  r0.zw = -g_viewport_origin.xy + v0.xy;
  r0.zw = g_viewport_dimensions.zw * r0.zw;
  gbuffer_colour_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r1.zw = r1.zw * r0.xy;
  r1.zw = floor(r1.zw);
  r1.zw = (int2)r1.zw;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r1.zw = max(int2(0,0), (int2)r1.zw);
  r1.xy = min((int2)r1.zw, (int2)r1.xy);
  r1.zw = float2(0,0);
  r2.xyz = gbuffer_colour_texture.Load(r1.xyw).xyz;
  r3.xyzw = gbuffer_constants_texture.Load(r1.xyw).xyzw;
  r4.z = gbuffer_readable_depth_texture.Load(r1.xyw).x;
  r2.w = 255 * r3.w;
  r2.w = round(r2.w);
  r2.w = (uint)r2.w;
  r5.xyz = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r2.xyz;
  r5.xyz = r3.xxx * r5.xyz + float3(0.0399999991,0.0399999991,0.0399999991);
  r3.x = 1 + -r3.x;
  r2.xyz = r3.xxx * r2.xyz;
  r2.xyz = float3(0.959999979,0.959999979,0.959999979) * r2.xyz;
  r6.xyz = float3(1,1,1) + -r5.xyz;
  r7.xyz = max(float3(0.00100000005,0.00100000005,0.00100000005), r6.xyz);
  r7.xyz = r2.xyz / r7.xyz;
  r4.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r2.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r2.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r2.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r2.xyz = r2.xyz / r0.zzz;
  t_shadow.GetDimensions(0, fDest.x, fDest.y, fDest.z);
  r0.zw = fDest.xy;
  r3.xw = g_render_target_dimensions.xy * r0.xy;
  r0.zw = float2(-1,-1) + r0.zw;
  r3.xw = max(float2(0,0), r3.xw);
  r0.zw = min(r3.xw, r0.zw);
  r4.xy = (uint2)r0.zw;
  r4.zw = float2(0,0);
  r0.z = RtDarker(t_rt_sun, r4.xy, t_shadow.Load(r4.xyz).x);
  sun_tinted = RtSunColour(sun_colour.xyz, r4.xy);
  r0.z = max(g_Shadow_low_clamp, r0.z);
  r0.z = min(1, r0.z);
  r0.w = cmp(2 < r2.y);
  r3.x = saturate(g_refraction_depth * -r2.y);
  r7.w = r0.w ? 1 : r3.x;
  r0.w = cmp(g_cloud_shadow_scale != 0.000000);
  if (r0.w != 0) {
    r3.xw = r2.xz / g_cloud_shadow_scale;
    r0.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
    r4.xy = g_cloud_shadow_direction.xy * r0.ww;
    r3.xw = r4.xy * time_in_sec + r3.xw;
    r4.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r3.xw, 0).xyzw;
    r4.xyzw = float4(1,1,1,1) + -r4.xyzw;
    r0.w = 4 * g_cloud_shadow_lerp;
    r0.w = min(1, abs(r0.w));
    r0.w = 1 + -r0.w;
    r8.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
    r8.xyzw = min(float4(1,1,1,1), abs(r8.xyzw));
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r0.w = r4.x * r8.x + r0.w;
    r0.w = r4.y * r8.y + r0.w;
    r0.w = r4.z * r8.z + r0.w;
    r0.w = r4.w * r8.w + r0.w;
  } else {
    r0.w = 1;
  }
  r4.xyz = (int3)r2.www & int3(1,16,128);
  if (r4.x != 0) {
    r1.xyz = gbuffer_normal_texture.Load(r1.xyz).xyz;
    r1.xy = r1.xy * float2(3.14159274,6.28318548) + float2(-1.57079637,-3.14159274);
    sincos(r1.xy, r1.xy, r8.xy);
    r9.x = r8.y;
    r9.z = r1.y;
    r8.xz = r9.xz * r8.xx;
    r1.y = 1 + -r3.y;
    r1.y = max(0, r1.y);
    r9.yz = min(float2(1,0.200000003), r1.yy);
    r3.xyw = -camera_position.xyz + r2.xyz;
    r1.y = dot(r3.xyw, r3.xyw);
    r1.y = rsqrt(r1.y);
    r10.xyz = r3.xyw * r1.yyy;
    r1.w = -0.200000003 + r0.z;
    r1.w = saturate(1.66666663 * r1.w);
    r4.x = r1.w * -2 + 3;
    r1.w = r1.w * r1.w;
    r1.w = r4.x * r1.w;
    r0.x = RtDarker(t_rt_ambient, g_render_target_dimensions.xy * r0.xy, t_ssao.SampleLevel(s_ssao_s, r0.xy, 0).x);
    r0.x = max(g_SSAO_low_clamp, r0.x);
    r0.x = min(1, r0.x);
    r0.y = min(r1.w, r0.w);
    r8.y = r1.x;
    r0.w = dot(r10.xyz, r8.xyz);
    r1.w = r0.w + r0.w;
    r11.xyz = r8.xyz * -r1.www + r10.xyz;
    if (g_use_spherical_harmonics != 0) {
      r4.xw = -g_probe_grid_area.xy + r2.xz;
      r12.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
      r4.xw = r4.xw / r12.xy;
      r1.w = g_probe_grid_size;
      r4.xw = r4.xw * r1.ww + float2(-0.5,-0.5);
      r1.w = -1 + r1.w;
      r4.xw = max(float2(0,0), r4.xw);
      r4.xw = min(r4.xw, r1.ww);
      r12.xy = floor(r4.xw);
      r4.xw = frac(r4.xw);
      r12.xy = (int2)r12.xy;
      r12.zw = (int2)r12.xy * int2(3,3);
      r1.w = mad(g_probe_grid_size, 3, -3);
      r12.xy = mad((int2)r12.xy, int2(3,3), int2(3,3));
      r12.xyzw = min((uint4)r12.xyzw, (uint4)r1.wwww);
      r13.xy = r8.zx * r1.xx;
      r14.xyz = r8.xzx * r8.zzx;
      r1.w = r14.y * 3 + -1;
      r5.w = -r1.x * r1.x + r14.z;
      r6.w = round(g_spherical_harmonic_terms);
      r6.w = (int)r6.w;
      r15.w = 0;
      r14.yzw = float3(0,0,0);
      r8.w = 0;
      while (true) {
        r9.w = cmp((int)r8.w >= (int)r6.w);
        if (r9.w != 0) break;
        r9.w = (uint)r8.w % 3;
        r15.xy = (int2)r9.ww + (int2)r12.xz;
        r15.z = (int)r12.w + (int)icb[r8.w+0].y;
        r16.xyz = t_world_spherical_harmonics.Load(r15.yzw).xyz;
        r17.xyz = t_world_spherical_harmonics.Load(r15.xzw).xyz;
        r18.y = (int)r12.y + (int)icb[r8.w+0].y;
        r18.xzw = r15.yww;
        r19.xyz = t_world_spherical_harmonics.Load(r18.xyz).xyz;
        r18.xzw = r15.xww;
        r15.xyz = t_world_spherical_harmonics.Load(r18.xyz).xyz;
        r17.xyz = r17.xyz + -r16.xyz;
        r16.xyz = r4.xxx * r17.xyz + r16.xyz;
        r15.xyz = r15.xyz + -r19.xyz;
        r15.xyz = r4.xxx * r15.xyz + r19.xyz;
        r15.xyz = r15.xyz + -r16.xyz;
        r15.xyz = r4.www * r15.xyz + r16.xyz;
        r16.xyzw = cmp((int4)r8.wwww == int4(0,1,2,3));
        r17.xyzw = cmp((int4)r8.wwww == int4(4,5,6,7));
        r9.w = cmp((int)r8.w == 8);
        r16.xyzw = r16.xyzw ? float4(1,1,1,1) : 0;
        r17.xyzw = r17.xyzw ? float4(1,1,1,1) : 0;
        r9.w = r9.w ? 1.000000 : 0;
        r10.w = r16.y * r8.x + r16.x;
        r10.w = r16.z * r1.x + r10.w;
        r10.w = r16.w * r8.z + r10.w;
        r10.w = r17.x * r14.x + r10.w;
        r10.w = r17.y * r13.x + r10.w;
        r10.w = r17.z * r13.y + r10.w;
        r10.w = r17.w * r1.w + r10.w;
        r9.w = r9.w * r5.w + r10.w;
        r14.yzw = r15.xyz * r9.www + r14.yzw;
        r8.w = (int)r8.w + 1;
      }
      r14.yzw = max(float3(0,0,0), r14.yzw);
      r8.w = cmp(g_specular_probe_lerp < 0.999000013);
      if (r8.w != 0) {
        r15.w = 0;
        r16.xyz = float3(0,0,0);
        r9.w = 0;
        while (true) {
          r10.w = cmp((int)r9.w >= (int)r6.w);
          if (r10.w != 0) break;
          r10.w = (uint)r9.w % 3;
          r15.xy = (int2)r10.ww + (int2)r12.xz;
          r15.z = (int)r12.w + (int)icb[r9.w+0].y;
          r17.xyz = t_prev_world_spherical_harmonics.Load(r15.yzw).xyz;
          r18.xyz = t_prev_world_spherical_harmonics.Load(r15.xzw).xyz;
          r19.y = (int)r12.y + (int)icb[r9.w+0].y;
          r19.xzw = r15.yww;
          r20.xyz = t_prev_world_spherical_harmonics.Load(r19.xyz).xyz;
          r19.xzw = r15.xww;
          r15.xyz = t_prev_world_spherical_harmonics.Load(r19.xyz).xyz;
          r18.xyz = r18.xyz + -r17.xyz;
          r17.xyz = r4.xxx * r18.xyz + r17.xyz;
          r15.xyz = r15.xyz + -r20.xyz;
          r15.xyz = r4.xxx * r15.xyz + r20.xyz;
          r15.xyz = r15.xyz + -r17.xyz;
          r15.xyz = r4.www * r15.xyz + r17.xyz;
          r17.xyzw = cmp((int4)r9.wwww == int4(0,1,2,3));
          r18.xyzw = cmp((int4)r9.wwww == int4(4,5,6,7));
          r10.w = cmp((int)r9.w == 8);
          r17.xyzw = r17.xyzw ? float4(1,1,1,1) : 0;
          r18.xyzw = r18.xyzw ? float4(1,1,1,1) : 0;
          r10.w = r10.w ? 1.000000 : 0;
          r13.z = r17.y * r8.x + r17.x;
          r13.z = r17.z * r1.x + r13.z;
          r13.z = r17.w * r8.z + r13.z;
          r13.z = r18.x * r14.x + r13.z;
          r13.z = r18.y * r13.x + r13.z;
          r13.z = r18.z * r13.y + r13.z;
          r13.z = r18.w * r1.w + r13.z;
          r10.w = r10.w * r5.w + r13.z;
          r16.xyz = r15.xyz * r10.www + r16.xyz;
          r9.w = (int)r9.w + 1;
        }
        r12.xyz = max(float3(0,0,0), r16.xyz);
        r13.xyz = r14.yzw + -r12.xyz;
        r12.xyz = g_specular_probe_lerp * r13.xyz + r12.xyz;
      }
      r12.xyz = r8.www ? r12.xyz : r14.yzw;
      r13.xyz = g_ambient_fudge_factor * r12.xyz;
      r4.xw = cmp(r8.xz < float2(0,0));
      r14.xyz = r4.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
      r1.w = cmp(r1.x < 0);
      r15.xyz = r1.www ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
      r16.xyz = r4.www ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
      r17.xyz = r8.xyz * r8.xyz;
      r15.xyz = r17.yyy * r15.xyz;
      r14.xyz = r17.xxx * r14.xyz + r15.xyz;
      r14.xyz = r17.zzz * r16.xyz + r14.xyz;
      r12.xyz = -r12.xyz * g_ambient_fudge_factor + r14.xyz;
      r12.xyz = g_cube_ambient_influence * r12.xyz + r13.xyz;
    } else {
      r4.xw = cmp(r8.xz < float2(0,0));
      r13.xyz = r4.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
      r1.w = cmp(r1.x < 0);
      r14.xyz = r1.www ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
      r15.xyz = r4.www ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
      r16.xyz = r8.xyz * r8.xyz;
      r14.xyz = r16.yyy * r14.xyz;
      r13.xyz = r16.xxx * r13.xyz + r14.xyz;
      r13.xyz = r16.zzz * r15.xyz + r13.xyz;
      r12.xyz = g_ambient_fudge_factor * r13.xyz;
    }
    r12.xyz = r12.xyz * r7.xyz;
    r12.xyz = r12.xyz * r6.xyz;
    r1.w = min(r0.x, r3.z);
    r3.z = 1 + -r9.y;
    r4.x = dot(r8.xyz, -r10.xyz);
    r9.x = max(0, r4.x);
    r4.w = min(1, r9.x);
    r5.w = r4.w * -5.55472994 + -6.98316002;
    r4.w = r5.w * r4.w;
    r4.w = exp2(r4.w);
    r5.w = 1 + -r3.z;
    r13.xyz = max(r5.www, r5.xyz);
    r13.xyz = r13.xyz + -r5.xyz;
    r13.xyz = r13.xyz * r4.www + r5.xyz;
    r14.xy = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r9.xy, 0).xy;
    r15.xyzw = (int4)r2.wwww & int4(128,8,2,16);
    r4.w = r15.x ? 1 : 0;
    r5.w = 1 + -r9.x;
    r4.w = r5.w * r4.w;
    r4.w = r4.w * -r14.x + r14.x;
    r13.xyz = r13.xyz * r4.www + r14.yyy;
    r3.z = 8 * r3.z;
    r3.z = min(7, r3.z);
    r9.xw = -g_probe_grid_area.xy + r2.xz;
    r14.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
    r9.xw = r9.xw / r14.xy;
    r4.w = g_probe_grid_size;
    r9.xw = r9.xw * r4.ww + float2(-0.5,-0.5);
    r5.w = -1 + r4.w;
    r9.xw = max(float2(0,0), r9.xw);
    r9.xw = min(r9.xw, r5.ww);
    r14.xy = floor(r9.xw);
    r9.xw = frac(r9.xw);
    r11.w = r14.y * r4.w + r14.x;
    r16.xyz = t_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, r3.z).xyz;
    r17.xyzw = float4(1,0,0,1) + r14.xyxy;
    r17.xw = r17.yw * r4.ww + r17.xz;
    r18.xyz = r11.xyz;
    r18.w = r17.x;
    r19.xyz = t_environment_array.SampleLevel(s_environment_array_s, r18.xyzw, r3.z).xyz;
    r17.xyz = r18.xyz;
    r20.xyz = t_environment_array.SampleLevel(s_environment_array_s, r17.xyzw, r3.z).xyz;
    r14.xy = float2(1,1) + r14.xy;
    r14.w = r14.y * r4.w + r14.x;
    r14.xyz = r17.xyz;
    r21.xyz = t_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r3.z).xyz;
    r19.xyz = r19.xyz + -r16.xyz;
    r16.xyz = r9.xxx * r19.xyz + r16.xyz;
    r19.xyz = r21.xyz + -r20.xyz;
    r19.xyz = r9.xxx * r19.xyz + r20.xyz;
    r19.xyz = r19.xyz + -r16.xyz;
    r16.xyz = r9.www * r19.xyz + r16.xyz;
    r4.w = cmp(g_specular_probe_lerp < 0.999000013);
    if (r4.w != 0) {
      r11.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r11.xyzw, r3.z).xyz;
      r19.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r18.xyzw, r3.z).xyz;
      r17.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r17.xyzw, r3.z).xyz;
      r14.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r3.z).xyz;
      r19.xyz = r19.xyz + -r11.xyz;
      r11.xyz = r9.xxx * r19.xyz + r11.xyz;
      r14.xyz = r14.xyz + -r17.xyz;
      r14.xyz = r9.xxx * r14.xyz + r17.xyz;
      r14.xyz = r14.xyz + -r11.xyz;
      r11.xyz = r9.www * r14.xyz + r11.xyz;
      r14.xyz = r16.xyz + -r11.xyz;
      r11.xyz = g_specular_probe_lerp * r14.xyz + r11.xyz;
    }
    r11.xyz = r4.www ? r11.xyz : r16.xyz;
    r11.xyz = r11.xyz * r13.xyz;
    r3.z = 1 + -r1.w;
    r3.z = r9.y * r3.z + r1.w;
    r11.xyz = r3.zzz * r11.xyz;
    r11.xyz = r11.xyz * r1.www;
    r11.xyz = r12.xyz * r1.www + r11.xyz;
    r1.w = dot(r8.xyz, -sun_direction.xyz);
    r3.z = saturate(r1.w);
    r4.w = cmp(0 < sun_specular);
    r4.w = r4.w ? r9.y : r9.z;
    r5.w = dot(-sun_direction.xyz, r18.xyz);
    r5.w = max(-1, r5.w);
    r5.w = min(1, r5.w);
    r6.w = 1 + -abs(r5.w);
    r6.w = sqrt(r6.w);
    r8.w = abs(r5.w) * -0.0187292993 + 0.0742610022;
    r8.w = r8.w * abs(r5.w) + -0.212114394;
    r8.w = r8.w * abs(r5.w) + 1.57072878;
    r9.x = r8.w * r6.w;
    r9.x = r9.x * -2 + 3.14159274;
    r5.w = cmp(r5.w < -r5.w);
    r5.w = r5.w ? r9.x : 0;
    r5.w = r8.w * r6.w + r5.w;
    r6.w = r4.w * -1.12524891 + 1.12839997;
    r9.xzw = float3(-0.0043633231,0.0043633231,0.0043633231) + r5.www;
    r5.w = 1 / r6.w;
    r9.xzw = r9.xzw * r5.www;
    r12.xy = float2(0.707106769,0.707106769) * r9.xz;
    r6.w = abs(r12.x) * abs(r12.x);
    r8.w = r6.w * abs(r12.x);
    r9.z = r8.w * abs(r12.x);
    r10.w = abs(r12.x) * 0.278393 + 1;
    r6.w = r6.w * 0.230388999 + r10.w;
    r6.w = r8.w * 0.000972000009 + r6.w;
    r6.w = r9.z * 0.0781079978 + r6.w;
    r6.w = r6.w * r6.w;
    r6.w = r6.w * r6.w;
    r6.w = 1 / r6.w;
    r6.w = 1 + -r6.w;
    r8.w = cmp(0 < r9.x);
    r9.x = cmp(r9.x < 0);
    r8.w = (int)-r8.w + (int)r9.x;
    r8.w = (int)r8.w;
    r6.w = r8.w * r6.w + 1;
    r6.w = 0.5 * r6.w;
    r8.w = r12.y * r12.y;
    r9.x = r8.w * r12.y;
    r9.z = r9.x * r12.y;
    r9.w = r9.w * 0.196853578 + 1;
    r8.w = r8.w * 0.230388999 + r9.w;
    r8.w = r9.x * 0.000972000009 + r8.w;
    r8.w = r9.z * 0.0781079978 + r8.w;
    r8.w = r8.w * r8.w;
    r8.w = r8.w * r8.w;
    r8.w = 1 / r8.w;
    r8.w = 2 + -r8.w;
    r6.w = r8.w * 0.5 + -r6.w;
    r9.xz = float2(-1.11072075,1.11072075) * r5.ww;
    r12.xy = r9.xz * r9.xz;
    r8.w = r12.x * -r9.x;
    r9.x = r8.w * -r9.x;
    r5.w = r5.w * 0.309216887 + 1;
    r12.xz = r12.xy * float2(0.230388999,0.230388999) + r5.ww;
    r5.w = r8.w * 0.000972000009 + r12.x;
    r5.w = r9.x * 0.0781079978 + r5.w;
    r5.w = r5.w * r5.w;
    r5.w = r5.w * r5.w;
    r5.w = 1 / r5.w;
    r5.w = 1 + -r5.w;
    r5.w = 1 + -r5.w;
    r5.w = 0.5 * r5.w;
    r8.w = r12.y * r9.z;
    r9.x = r8.w * r9.z;
    r8.w = r8.w * 0.000972000009 + r12.z;
    r8.w = r9.x * 0.0781079978 + r8.w;
    r8.w = r8.w * r8.w;
    r8.w = r8.w * r8.w;
    r8.w = 1 / r8.w;
    r8.w = 2 + -r8.w;
    r5.w = r8.w * 0.5 + -r5.w;
    r5.w = abs(r6.w) / r5.w;
    r5.w = min(1, r5.w);
    r5.w = r5.w * r5.w;
    r5.w = 0.785398185 * r5.w;
    r4.w = 1 + -r4.w;
    r6.w = 1.57079637 * r4.w;
    r6.w = sin(r6.w);
    r8.w = -1 + r3.z;
    r6.w = r6.w * r8.w + 1;
    r0.w = saturate(r0.w);
    r4.w = r4.w * r4.w;
    r8.w = 0.5 * r4.w;
    r4.w = -r4.w * 0.5 + 1;
    r9.x = r3.z * r4.w + r8.w;
    r9.x = 1 / r9.x;
    r0.w = r0.w * r4.w + r8.w;
    r0.w = 1 / r0.w;
    r0.w = r9.x * r0.w;
    r0.w = 0.25 * r0.w;
    r0.w = min(1, r0.w);
    r0.w = r0.w * r6.w;
    r3.xyw = -r3.xyw * r1.yyy + -sun_direction.xyz;
    r1.y = dot(r3.xyw, r3.xyw);
    r1.y = rsqrt(r1.y);
    r3.xyw = r3.xyw * r1.yyy;
    r1.y = saturate(dot(r3.xyw, -sun_direction.xyz));
    r3.x = r1.y * -5.55472994 + -6.98316002;
    r1.y = r3.x * r1.y;
    r1.y = exp2(r1.y);
    r3.xyw = r6.xyz * r1.yyy + r5.xyz;
    r0.w = r5.w * r0.w;
    r3.xyw = r0.www * r3.xyw;
    r3.xyw = sun_tinted * r3.xyw;
    r9.xzw = g_light_scale_for_units * r3.xyw;
    r3.xyw = r15.yyy ? r9.xzw : r3.xyw;
    r9.xzw = float3(-1,-1,-1) + g_water_reflection_tint.xyz;
    r9.xzw = g_water_reflection_tint.www * r9.xzw + float3(1,1,1);
    r9.xzw = r9.xzw * r3.xyw;
    r9.xzw = g_water_reflection_strength * r9.xzw;
    r12.xyz = float3(10,10,10) * r9.xzw;
    r9.xzw = r15.www ? r12.xyz : r9.xzw;
    r3.xyw = r15.zzz ? r9.xzw : r3.xyw;
    if (r15.x != 0) r3.xyw *= SI.foliage_glints;
    r0.w = r3.z * -5.55472994 + -6.98316002;
    r0.w = r0.w * r3.z;
    r0.w = exp2(r0.w);
    r5.xyz = r6.xyz * r0.www + r5.xyz;
    r5.xyz = float3(1,1,1) + -r5.xyz;
    if (r15.x != 0) {
      r0.w = r1.w * 0.5 + 0.5;
      r1.y = r0.w * r0.w;
      r0.w = -r0.w * r0.w + 1;
      r0.w = g_vegetation_wrap_lighting_bias * r0.w + r1.y;
      r6.xyz = r7.xyz * r0.www;
      r6.xyz = sun_tinted * r6.xyz;
      r6.xyz = r6.xyz * r5.xyz;
      r0.w = dot(sun_direction.xyz, r10.xyz);
      r1.y = max(0.100000001, r1.w);
      r1.y = 0.00100000005 / r1.y;
      r4.w = cmp(r1.w < 0);
      r4.w = r4.w ? 1.000000 : 0;
      r1.y = r4.w * r1.y;
      r9.xzw = float3(0.800000012,0.899999976,0.800000012) + -r7.xyz;
      r9.xzw = r9.xzw * float3(0.100000001,0.100000001,0.100000001) + r7.xyz;
      r9.xzw = float3(150,150,150) * r9.xzw;
      r4.w = dot(sun_direction.xyz, r8.xyz);
      r4.w = max(0, r4.w);
      r9.xzw = -r9.xzw * r1.yyy;
      r9.xzw = float3(1.44269502,1.44269502,1.44269502) * r9.xzw;
      r9.xzw = exp2(r9.xzw);
      r10.xyz = r9.xzw * r4.www;
      r10.xyz = float3(0.318309873,0.318309873,0.318309873) * r10.xyz;
      r12.xyz = float3(1,1,1) + -r7.xyz;
      r0.w = r0.w * -0.958400011 + 1;
      r0.w = r0.w * r0.w;
      r0.w = 12.566371 * r0.w;
      r0.w = 0.0814694166 / r0.w;
      r9.xzw = float3(1,1,1) + -r9.xzw;
      r9.xzw = r9.xzw * r0.www;
      r13.xyz = sun_tinted * r7.xyz;
      r13.xyz = float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * r13.xyz;
      r9.xzw = r10.xyz * r12.xyz + r9.xzw;
      r9.xzw = r13.xyz * r9.xzw;
      r10.xyz = r0.xxx * r7.xyz;
      if (g_use_spherical_harmonics != 0) {
        t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
        r0.xw = uiDest.xy;
        uiDest.xw = (uint2)r0.xw / int2(3,3);
        r0.xw = uiDest.xw;
        r0.xw = (uint2)r0.xw;
        r0.xw = float2(0.5,0.5) * r0.xw;
        r0.xw = trunc(r0.xw);
        r12.xy = r8.zx * r1.xx;
        r13.xyz = r8.xzx * r8.zzx;
        r1.y = r13.y * 3 + -1;
        r1.x = -r1.x * r1.x + r13.z;
        r4.w = round(g_spherical_harmonic_terms);
        r4.w = (int)r4.w;
        r14.zw = float2(0,0);
        r13.yzw = float3(0,0,0);
        r5.w = 0;
        while (true) {
          r6.w = cmp((int)r5.w >= (int)r4.w);
          if (r6.w != 0) break;
          r6.w = (uint)r5.w % 3;
          r6.w = (int)r6.w;
          r6.w = r0.x * 3 + r6.w;
          r8.w = (int)icb[r5.w+0].y;
          r8.w = r0.w * 3 + r8.w;
          r14.x = (int)r6.w;
          r14.y = (int)r8.w;
          r15.xyz = t_world_spherical_harmonics.Load(r14.xyz).xyz;
          r16.xyzw = cmp((int4)r5.wwww == int4(0,1,2,3));
          r17.xyzw = cmp((int4)r5.wwww == int4(4,5,6,7));
          r6.w = cmp((int)r5.w == 8);
          r16.xyzw = r16.xyzw ? float4(1,1,1,1) : 0;
          r17.xyzw = r17.xyzw ? float4(1,1,1,1) : 0;
          r6.w = r6.w ? 1.000000 : 0;
          r8.w = r16.y * -r8.x + r16.x;
          r8.w = r16.z * -r8.y + r8.w;
          r8.w = r16.w * -r8.z + r8.w;
          r8.w = r17.x * r13.x + r8.w;
          r8.w = r17.y * r12.x + r8.w;
          r8.w = r17.z * r12.y + r8.w;
          r8.w = r17.w * r1.y + r8.w;
          r6.w = r6.w * r1.x + r8.w;
          r13.yzw = r15.xyz * r6.www + r13.yzw;
          r5.w = (int)r5.w + 1;
        }
        r12.xyz = max(float3(0,0,0), r13.yzw);
        r12.xyz = g_ambient_fudge_factor * r12.xyz;
      } else {
        r13.xyz = cmp(-r8.xyz < float3(0,0,0));
        r14.xyz = r13.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
        r13.xyw = r13.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
        r15.xyz = r13.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
        r8.xyz = r8.xyz * r8.xyz;
        r13.xyz = r8.yyy * r13.xyw;
        r8.xyw = r8.xxx * r14.xyz + r13.xyz;
        r8.xyz = r8.zzz * r15.xyz + r8.xyw;
        r12.xyz = g_ambient_fudge_factor * r8.xyz;
      }
      r8.xyz = r12.xyz * r10.xyz;
    } else {
      r0.x = r9.y * -1.12524891 + 1.12839997;
      r0.w = r0.x * r0.x;
      r1.xy = r0.xx * r0.xx + float2(0.330000013,0.0900000036);
      r0.x = r1.x + r1.x;
      r0.x = r0.w / r0.x;
      r0.x = 1 + -r0.x;
      r0.w = 0.449999988 * r0.w;
      r0.w = r0.w / r1.y;
      r1.x = max(-1, r1.w);
      r1.y = max(-1, r4.x);
      r1.xy = min(float2(1,1), r1.xy);
      r1.w = 1 + -abs(r1.x);
      r1.w = sqrt(r1.w);
      r4.x = abs(r1.x) * -0.0187292993 + 0.0742610022;
      r4.x = r4.x * abs(r1.x) + -0.212114394;
      r4.x = r4.x * abs(r1.x) + 1.57072878;
      r4.w = r4.x * r1.w;
      r4.w = r4.w * -2 + 3.14159274;
      r5.w = cmp(r1.x < -r1.x);
      r4.w = r5.w ? r4.w : 0;
      r1.w = r4.x * r1.w + r4.w;
      r4.x = 1 + -abs(r1.y);
      r4.x = sqrt(r4.x);
      r4.w = abs(r1.y) * -0.0187292993 + 0.0742610022;
      r4.w = r4.w * abs(r1.y) + -0.212114394;
      r4.w = r4.w * abs(r1.y) + 1.57072878;
      r5.w = r4.w * r4.x;
      r5.w = r5.w * -2 + 3.14159274;
      r1.y = cmp(r1.y < -r1.y);
      r1.y = r1.y ? r5.w : 0;
      r1.y = r4.w * r4.x + r1.y;
      r4.x = max(r1.y, r1.w);
      r4.w = min(r1.y, r1.w);
      r1.y = r1.w + -r1.y;
      r1.y = cos(r1.y);
      r1.y = max(0, r1.y);
      r0.w = r1.y * r0.w;
      r1.y = sin(r4.x);
      r0.w = r1.y * r0.w;
      sincos(r4.w, r4.x, r10.x);
      r1.y = r4.x / r10.x;
      r0.x = r0.w * r1.y + r0.x;
      r0.w = max(0, r1.x);
      r0.x = r0.x * r0.w;
      r1.xyw = sun_tinted * r5.xyz;
      r1.xyw = r1.xyw * r7.xyz;
      r0.w = cmp(0.5 < g_camera_temp0.x);
      r0.x = r0.w ? r3.z : r0.x;
      r6.xyz = r1.xyw * r0.xxx;
      r9.xzw = float3(0,0,0);
      r8.xyz = float3(0,0,0);
    }
    r0.x = (int)r2.w & 64;
    r1.xyw = float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * r6.xyz;
    r1.xyw = r0.xxx ? float3(1.90384671e-005,0,0) : r1.xyw;
    r1.xyw = r3.xyw + r1.xyw;
    r1.xyw = r1.xyw + r9.xzw;
    r1.xyw = r1.xyw + r8.xyz;
    r3.xyz = r7.xyz * r1.zzz;
    r0.xyw = r0.yyy * r1.xyw + r11.xyz;
    r7.xyz = r3.xyz * float3(255,255,255) + r0.xyw;
  }
  r0.x = cmp((int)r4.y != 0);
  r0.y = cmp((int)r4.z == 0);
  r0.x = r0.y ? r0.x : 0;
  if (r0.x != 0) {
    r0.xyw = float3(0,1,0) * r2.yyy;
    r0.xyw = r0.xyw / float3(1,1,1);
    r1.xyz = r2.xyz + -r0.xyw;
    r1.w = 1;
    r0.x = dot(r1.xyzw, g_overlay_view_projection._m00_m10_m20_m30);
    r0.y = dot(r1.xyzw, g_overlay_view_projection._m01_m11_m21_m31);
    r0.w = dot(r1.xyzw, g_overlay_view_projection._m03_m13_m23_m33);
    r0.xy = r0.xy / r0.ww;
    r0.xy = r0.xy * float2(0.5,-0.5) + float2(0.5,0.5);
    r0.xy = r0.xy * g_viewport_dimensions.xy + g_viewport_origin.xy;
    r0.xy = screen_size.zw * r0.xy;
    r0.xy /= min(g_viewport_dimensions.xy * screen_size.zw, 1.f);  // RenoDX
    r1.xyzw = s_overlay.SampleLevel(s_overlay_s, r0.xy, r0.y).xyzw;
    r0.x = cmp(0 != g_shroud_enabled);
    if (r0.x != 0) {
      r0.xy = -g_campaign_world_bounds.xy + r2.xz;
      r3.xy = g_campaign_world_bounds.zw + -g_campaign_world_bounds.xy;
      r0.xy = r0.xy / r3.xy;
      r0.x = s_shroud.SampleLevel(s_shroud_s, r0.xy, r0.y).x;
    } else {
      r0.x = 1;
    }
    if (g_use_spherical_harmonics != 0) {
      r0.yw = -g_probe_grid_area.xy + r2.xz;
      r2.xz = g_probe_grid_area.zw + -g_probe_grid_area.xy;
      r0.yw = r0.yw / r2.xz;
      r2.x = g_probe_grid_size;
      r0.yw = r0.yw * r2.xx + float2(-0.5,-0.5);
      r2.x = -1 + r2.x;
      r0.yw = max(float2(0,0), r0.yw);
      r0.yw = min(r0.yw, r2.xx);
      r2.xz = floor(r0.yw);
      r0.yw = frac(r0.yw);
      r2.xz = (int2)r2.xz;
      r3.xy = (int2)r2.xz * int2(3,3);
      r2.w = mad(g_probe_grid_size, 3, -3);
      r3.xy = min((uint2)r3.xy, (uint2)r2.ww);
      r2.xz = mad((int2)r2.xz, int2(3,3), int2(3,3));
      r2.xz = min((uint2)r2.xz, (uint2)r2.ww);
      r2.w = round(g_spherical_harmonic_terms);
      r2.w = (int)r2.w;
      r4.w = 0;
      r5.xyz = float3(0,0,0);
      r3.z = 0;
      while (true) {
        r3.w = cmp((int)r3.z >= (int)r2.w);
        if (r3.w != 0) break;
        r3.w = (uint)r3.z % 3;
        r4.y = (int)r3.w + (int)r3.x;
        r4.z = (int)r3.y + (int)icb[r3.z+0].y;
        r6.xyz = t_world_spherical_harmonics.Load(r4.yzw).xyz;
        r4.x = (int)r2.x + (int)r3.w;
        r8.xyz = t_world_spherical_harmonics.Load(r4.xzw).xyz;
        r9.y = (int)r2.z + (int)icb[r3.z+0].y;
        r9.xzw = r4.yww;
        r10.xyz = t_world_spherical_harmonics.Load(r9.xyz).xyz;
        r9.xzw = r4.xww;
        r4.xyz = t_world_spherical_harmonics.Load(r9.xyz).xyz;
        r8.xyz = r8.xyz + -r6.xyz;
        r6.xyz = r0.yyy * r8.xyz + r6.xyz;
        r4.xyz = r4.xyz + -r10.xyz;
        r4.xyz = r0.yyy * r4.xyz + r10.xyz;
        r4.xyz = r4.xyz + -r6.xyz;
        r4.xyz = r0.www * r4.xyz + r6.xyz;
        r5.xyz = r4.xyz * icb[r3.z+0].zzz + r5.xyz;
        r3.z = (int)r3.z + 1;
      }
      r4.xyz = max(float3(0,0,0), r5.xyz);
      r3.z = cmp(g_specular_probe_lerp < 0.999000013);
      if (r3.z != 0) {
        r5.w = 0;
        r6.xyz = float3(0,0,0);
        r3.w = 0;
        while (true) {
          r4.w = cmp((int)r3.w >= (int)r2.w);
          if (r4.w != 0) break;
          r4.w = (uint)r3.w % 3;
          r5.y = (int)r3.x + (int)r4.w;
          r5.z = (int)r3.y + (int)icb[r3.w+0].y;
          r8.xyz = t_prev_world_spherical_harmonics.Load(r5.yzw).xyz;
          r5.x = (int)r2.x + (int)r4.w;
          r9.xyz = t_prev_world_spherical_harmonics.Load(r5.xzw).xyz;
          r10.y = (int)r2.z + (int)icb[r3.w+0].y;
          r10.xzw = r5.yww;
          r11.xyz = t_prev_world_spherical_harmonics.Load(r10.xyz).xyz;
          r10.xzw = r5.xww;
          r5.xyz = t_prev_world_spherical_harmonics.Load(r10.xyz).xyz;
          r9.xyz = r9.xyz + -r8.xyz;
          r8.xyz = r0.yyy * r9.xyz + r8.xyz;
          r5.xyz = r5.xyz + -r11.xyz;
          r5.xyz = r0.yyy * r5.xyz + r11.xyz;
          r5.xyz = r5.xyz + -r8.xyz;
          r5.xyz = r0.www * r5.xyz + r8.xyz;
          r6.xyz = r5.xyz * icb[r3.w+0].zzz + r6.xyz;
          r3.w = (int)r3.w + 1;
        }
        r2.xzw = max(float3(0,0,0), r6.xyz);
        r3.xyw = r4.xyz + -r2.xzw;
        r2.xzw = g_specular_probe_lerp * r3.xyw + r2.xzw;
      }
      r2.xzw = r3.zzz ? r2.xzw : r4.xyz;
      r2.xzw = g_ambient_fudge_factor * r2.xzw;
    } else {
      r2.xzw = g_ambient_fudge_factor * ambient_cube_tb[0].xyz;
    }
    r3.xyz = float3(1,1,1) + -r2.xzw;
    r0.yzw = r0.zzz * r3.xyz + r2.xzw;
    r0.x = saturate(-0.5 + r0.x);
    r0.x = r0.x * 1.89999998 + 0.0500000007;
    r1.xyz = r1.xyz * r0.xxx;
    r0.x = cmp(r2.y < 0);
    r0.x = r0.x ? 0 : 1;
    r2.xyz = r7.xyz * r1.www;
    r0.yzw = r0.yzw * r1.xyz + r2.xyz;
    r0.yzw = r0.yzw + -r7.xyz;
    r7.xyz = r0.xxx * r0.yzw + r7.xyz;
  }
  o0.xyzw = max(float4(0,0,0,0), r7.xyzw);
  return;
}