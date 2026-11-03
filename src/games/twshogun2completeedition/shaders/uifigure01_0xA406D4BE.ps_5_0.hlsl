// rigidstandard.hlsl ps30_main_ui (decompiled): the 3D figures in the menus (clan leaders, generals). Their target is the UI's
// (8-bit in the game: every output clamped to 0-1), upgraded to 16-bit by the mod: its colour kept above 1, the figure turned
// see-through over the menu (ATN debug22). RenoDX: the output clamped as the 8-bit target did.

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

cbuffer lighting_VS_PS : register(b2)
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

cbuffer fog : register(b3)
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
  float g_fog_shadow_strength : packoffset(c4.z);
  float g_fog_blur_offset : packoffset(c4.w);
  float g_fog_blur_strength : packoffset(c5);
  float2 g_force_fog : packoffset(c5.y);
}

cbuffer rigid_config_PS : register(b4)
{
  float g_unlit : packoffset(c0);
  float g_dissolve_enable : packoffset(c0.y);
  float g_untextured : packoffset(c0.z);
  float snow : packoffset(c0.w);
  float rain : packoffset(c1);
  float g_tesselation_factor : packoffset(c1.y);
  float g_tesselation_displacement_scale : packoffset(c1.z);
  float g_rain_rigid_roughness_multiplier : packoffset(c1.w);
  float g_rain_vegetation_roughness_multiplier : packoffset(c2);
}

cbuffer rigid_lighting_PS : register(b5)
{
  float g_Ks_gloss_factor : packoffset(c0);
  float g_Exps_gloss_factor : packoffset(c0.y);
  float g_Ks_colour_factor : packoffset(c0.z);
  float g_Ks_colour_saturate_factor : packoffset(c0.w);
  float g_Kf_colour_factor : packoffset(c1);
  float g_Ks : packoffset(c1.y);
  float g_Exps : packoffset(c1.z);
  float g_Kf : packoffset(c1.w);
  float g_Expf : packoffset(c2);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_ibl_brdf_lut_s : register(s1);
SamplerState s_cloud_shadow_s : register(s2);
SamplerState s_volume_noise_s : register(s3);
SamplerState s_2d_noise_s : register(s4);
SamplerState s_diffuse_map_s : register(s5);
SamplerState s_normal_map_s : register(s6);
SamplerState s_gloss_map_s : register(s7);
SamplerState s_dissolve_map_s : register(s8);
Texture2D<float4> s_diffuse_map : register(t0);
Texture2D<float4> s_dissolve_map : register(t1);
Texture2D<float4> s_gloss_map : register(t2);
Texture2D<float4> s_normal_map : register(t3);
TextureCubeArray<float4> t_environment_array : register(t4);
TextureCubeArray<float4> t_prev_environment_array : register(t5);
Texture2D<float4> t_world_spherical_harmonics : register(t6);
Texture2D<float4> t_prev_world_spherical_harmonics : register(t7);
Texture2D<float4> t_ibl_brdf_lut : register(t8);
Texture2D<float4> t_cloud_shadow : register(t9);
Texture3D<float4> t_volume_noise : register(t10);
Texture2D<float4> t_2d_noise : register(t11);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  float4 v9 : COLOR0,
  float4 v10 : COLOR1,
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(g_Ks_gloss_factor >= 0);
  r0.y = floor(v6.w);
  r0.z = 0.00100000005 * r0.y;
  r0.w = cmp(0 < v6.x);
  r1.x = cmp(v6.x < 0);
  r0.w = (int)-r0.w + (int)r1.x;
  r0.w = (int)r0.w;
  r0.w = cmp(r0.w < 0);
  r1.xyzw = s_diffuse_map.Sample(s_diffuse_map_s, v2.xy).wxyz;
  r2.xyz = cmp(float3(0,0,0) != float3(g_unlit, g_dissolve_enable, g_untextured));
  if (r2.y != 0) {
    r2.yw = v7.xy / v7.zz;
    r2.yw = r2.yw * float2(0.5,-0.5) + float2(0.5,0.5);
    r2.yw = screen_size.xy * r2.yw;
    r2.yw = float2(0.015625,0.015625) * r2.yw;
    r2.y = s_dissolve_map.Sample(s_dissolve_map_s, r2.yw).x;
    r0.y = -r0.y * 0.00100000005 + 1;
    r1.x = -r2.y * r0.y + r0.z;
  } else {
    r0.y = r2.x ? 1 : r0.z;
    r1.x = r1.x * r0.y;
  }
  r1.xyzw = r2.zzzz ? v10.xyzw : r1.yzwx;
  r3.xyzw = s_gloss_map.Sample(s_gloss_map_s, v2.xy).xyzw;
  r2.yzw = r1.xyz * abs(v6.xyz) + -r1.xyz;
  r2.yzw = r3.zzz * r2.yzw + r1.xyz;
  r0.y = dot(v4.xyz, v4.xyz);
  r0.y = rsqrt(r0.y);
  r4.xyz = v4.xyz * r0.yyy;
  r0.y = dot(v5.xyz, v5.xyz);
  r0.y = rsqrt(r0.y);
  r5.xyz = v5.xyz * r0.yyy;
  r0.y = dot(v3.xyz, v3.xyz);
  r0.y = rsqrt(r0.y);
  r6.xyz = v3.xyz * r0.yyy;
  if (r0.x != 0) {
    r7.xyz = s_normal_map.Sample(s_normal_map_s, v2.xy).xzw;
    r7.xyz = r7.xzy * float3(2,2,2) + float3(-1,-1,-1);
  } else {
    r0.yz = s_normal_map.Sample(s_normal_map_s, v2.xy).yw;
    r0.yz = float2(0.00196078443,0.00196078443) + r0.zy;
    r7.xy = r0.yz * float2(2,2) + float2(-1,-1);
    r0.y = dot(r7.xy, r7.xy);
    r0.y = 1 + -r0.y;
    r0.y = max(0, r0.y);
    r7.z = sqrt(r0.y);
  }
  r5.xyz = r7.yyy * r5.xyz;
  r4.xyz = r7.xxx * r4.xyz + r5.xyz;
  r4.xyz = r7.zzz * r6.xyz + r4.xyz;
  r0.y = dot(r4.xyz, r4.xyz);
  r0.y = rsqrt(r0.y);
  r4.xzw = r4.xyz * r0.yyy;
  r5.y = r3.y * r3.y;
  r5.z = -r3.x * r3.x + 1;
  r5.w = 1;
  r3.xyz = r0.xxx ? r5.yzw : r3.xyw;
  r0.x = saturate(r4.y * r0.y + -0.600000024);
  r0.y = r0.x * -2 + 3;
  r0.x = r0.x * r0.x;
  r0.x = r0.y * r0.x;
  r0.x = 1.5 * r0.x;
  r0.y = saturate(1.5 * r3.y);
  r0.x = r0.x * r0.y;
  r0.x = min(1, r0.x);
  r5.xyz = float3(0.5,0.5,0.649999976) + -r2.yzw;
  r0.xyz = r0.xxx * r5.xyz + r2.yzw;
  r1.xyz = r0.www ? r0.xyz : r2.yzw;
  r0.x = r2.x ? 0 : 1;
  if (r0.x != 0) {
    r0.yzw = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r1.xyz;
    r0.yzw = r3.xxx * r0.yzw + float3(0.0399999991,0.0399999991,0.0399999991);
    r2.x = 1 + -r3.x;
    r2.xyz = r2.xxx * r1.xyz;
    r2.xyz = float3(0.959999979,0.959999979,0.959999979) * r2.xyz;
    r5.xyz = float3(1,1,1) + -r0.yzw;
    r6.xyz = max(float3(0.00100000005,0.00100000005,0.00100000005), r5.xyz);
    r2.xyz = r2.xyz / r6.xyz;
    r6.xyz = -camera_position.xyz + v1.xyz;
    r2.w = dot(r6.xyz, r6.xyz);
    r3.x = rsqrt(r2.w);
    r7.xyz = r6.xyz * r3.xxx;
    r3.w = cmp(g_cloud_shadow_scale != 0.000000);
    if (r3.w != 0) {
      r8.xy = v1.xz / g_cloud_shadow_scale;
      r3.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
      r8.zw = g_cloud_shadow_direction.xy * r3.ww;
      r8.xy = r8.zw * time_in_sec + r8.xy;
      r8.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r8.xy, 0).xyzw;
      r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
      r3.w = 4 * g_cloud_shadow_lerp;
      r3.w = min(1, abs(r3.w));
      r3.w = 1 + -r3.w;
      r9.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
      r9.xyzw = min(float4(1,1,1,1), abs(r9.xyzw));
      r9.xyzw = float4(1,1,1,1) + -r9.xyzw;
      r3.w = r8.x * r9.x + r3.w;
      r3.w = r8.y * r9.y + r3.w;
      r3.w = r8.z * r9.z + r3.w;
      r3.w = r8.w * r9.w + r3.w;
    } else {
      r3.w = 1;
    }
    if (r0.x != 0) {
      r0.x = 1 + -r3.y;
      r0.x = max(0, r0.x);
      r8.zw = min(float2(1,0.200000003), r0.xx);
      r0.x = min(1, r3.w);
      r3.y = dot(r7.xyz, r4.xzw);
      r3.w = r3.y + r3.y;
      r9.xyz = r4.xzw * -r3.www + r7.xyz;
      if (g_use_spherical_harmonics != 0) {
        t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
        r10.xy = uiDest.xy;
        uiDest.xy = (uint2)r10.xy / int2(3,3);
        r10.xy = uiDest.xy;
        r10.xy = (uint2)r10.xy;
        r10.xy = float2(0.5,0.5) * r10.xy;
        r10.xy = trunc(r10.xy);
        r11.xyzw = r4.xwzw * r4.wzxw;
        r3.w = r11.w * 3 + -1;
        r4.y = r4.z * r4.z;
        r4.y = r4.x * r4.x + -r4.y;
        r5.w = round(g_spherical_harmonic_terms);
        r5.w = (int)r5.w;
        r12.zw = float2(0,0);
        r13.xyz = float3(0,0,0);
        r6.w = 0;
        while (true) {
          r7.w = cmp((int)r6.w >= (int)r5.w);
          if (r7.w != 0) break;
          r7.w = (uint)r6.w % 3;
          r7.w = (int)r7.w;
          r7.w = r10.x * 3 + r7.w;
          r10.z = (int)icb[r6.w+0].y;
          r10.z = r10.y * 3 + r10.z;
          r12.x = (int)r7.w;
          r12.y = (int)r10.z;
          r14.xyz = t_world_spherical_harmonics.Load(r12.xyz).xyz;
          r15.xyzw = cmp((int4)r6.wwww == int4(0,1,2,3));
          r16.xyzw = cmp((int4)r6.wwww == int4(4,5,6,7));
          r7.w = cmp((int)r6.w == 8);
          r15.xyzw = r15.xyzw ? float4(1,1,1,1) : 0;
          r16.xyzw = r16.xyzw ? float4(1,1,1,1) : 0;
          r7.w = r7.w ? 1.000000 : 0;
          r10.z = r15.y * r4.x + r15.x;
          r10.z = r15.z * r4.z + r10.z;
          r10.z = r15.w * r4.w + r10.z;
          r10.z = r16.x * r11.x + r10.z;
          r10.z = r16.y * r11.y + r10.z;
          r10.z = r16.z * r11.z + r10.z;
          r10.z = r16.w * r3.w + r10.z;
          r7.w = r7.w * r4.y + r10.z;
          r13.xyz = r14.xyz * r7.www + r13.xyz;
          r6.w = (int)r6.w + 1;
        }
        r10.xyz = max(float3(0,0,0), r13.xyz);
        r10.xyz = g_ambient_fudge_factor * r10.xyz;
      } else {
        r11.xyz = cmp(r4.xzw < float3(0,0,0));
        r12.xyz = r11.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
        r11.xyw = r11.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
        r13.xyz = r11.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
        r14.xyz = r4.xzw * r4.xzw;
        r11.xyz = r14.yyy * r11.xyw;
        r11.xyz = r14.xxx * r12.xyz + r11.xyz;
        r11.xyz = r14.zzz * r13.xyz + r11.xyz;
        r10.xyz = g_ambient_fudge_factor * r11.xyz;
      }
      r10.xyz = r10.xyz * r2.xyz;
      r10.xyz = r10.xyz * r5.xyz;
      r3.z = min(1, r3.z);
      r3.w = 1 + -r8.z;
      r4.y = dot(r4.xzw, -r7.xyz);
      r8.xy = max(float2(0,-1), r4.yy);
      r11.xy = min(float2(1,1), r8.xy);
      r4.y = r11.x * -5.55472994 + -6.98316002;
      r4.y = r4.y * r11.x;
      r4.y = exp2(r4.y);
      r5.w = 1 + -r3.w;
      r11.xzw = max(r5.www, r0.yzw);
      r11.xzw = r11.xzw + -r0.yzw;
      r11.xzw = r11.xzw * r4.yyy + r0.yzw;
      r8.xy = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r8.xz, 0).xy;
      r11.xzw = r11.xzw * r8.xxx + r8.yyy;
      r3.w = 8 * r3.w;
      r3.w = min(7, r3.w);
      r8.xy = -g_probe_grid_area.xy + v1.xz;
      r12.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
      r8.xy = r8.xy / r12.xy;
      r4.y = g_probe_grid_size;
      r8.xy = r8.xy * r4.yy + float2(-0.5,-0.5);
      r5.w = -1 + r4.y;
      r8.xy = max(float2(0,0), r8.xy);
      r8.xy = min(r8.xy, r5.ww);
      r12.xy = floor(r8.xy);
      r8.xy = frac(r8.xy);
      r9.w = r12.y * r4.y + r12.x;
      r13.xyz = t_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, r3.w).xyz;
      r14.xyzw = float4(1,0,0,1) + r12.xyxy;
      r14.xw = r14.yw * r4.yy + r14.xz;
      r15.xyz = r9.xyz;
      r15.w = r14.x;
      r16.xyz = t_environment_array.SampleLevel(s_environment_array_s, r15.xyzw, r3.w).xyz;
      r14.xyz = r15.xyz;
      r17.xyz = t_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r3.w).xyz;
      r12.xy = float2(1,1) + r12.xy;
      r12.w = r12.y * r4.y + r12.x;
      r12.xyz = r14.xyz;
      r18.xyz = t_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, r3.w).xyz;
      r16.xyz = r16.xyz + -r13.xyz;
      r13.xyz = r8.xxx * r16.xyz + r13.xyz;
      r16.xyz = r18.xyz + -r17.xyz;
      r16.xyz = r8.xxx * r16.xyz + r17.xyz;
      r16.xyz = r16.xyz + -r13.xyz;
      r13.xyz = r8.yyy * r16.xyz + r13.xyz;
      r4.y = cmp(g_specular_probe_lerp < 0.999000013);
      if (r4.y != 0) {
        r9.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r9.xyzw, r3.w).xyz;
        r16.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r15.xyzw, r3.w).xyz;
        r14.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r3.w).xyz;
        r12.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r12.xyzw, r3.w).xyz;
        r16.xyz = r16.xyz + -r9.xyz;
        r9.xyz = r8.xxx * r16.xyz + r9.xyz;
        r12.xyz = r12.xyz + -r14.xyz;
        r12.xyz = r8.xxx * r12.xyz + r14.xyz;
        r12.xyz = r12.xyz + -r9.xyz;
        r9.xyz = r8.yyy * r12.xyz + r9.xyz;
        r12.xyz = r13.xyz + -r9.xyz;
        r9.xyz = g_specular_probe_lerp * r12.xyz + r9.xyz;
      }
      r9.xyz = r4.yyy ? r9.xyz : r13.xyz;
      r9.xyz = r9.xyz * r11.xzw;
      r3.w = 1 + -r3.z;
      r3.w = r8.z * r3.w + r3.z;
      r9.xyz = r3.www * r9.xyz;
      r9.xyz = r9.xyz * r3.zzz;
      r9.xyz = r10.xyz * r3.zzz + r9.xyz;
      r3.z = dot(r4.xzw, -sun_direction.xyz);
      r3.yw = saturate(r3.yz);
      r4.x = cmp(0 < sun_specular);
      r4.x = r4.x ? r8.z : r8.w;
      r4.y = dot(-sun_direction.xyz, r15.xyz);
      r4.y = max(-1, r4.y);
      r4.y = min(1, r4.y);
      r4.z = 1 + -abs(r4.y);
      r4.z = sqrt(r4.z);
      r4.w = abs(r4.y) * -0.0187292993 + 0.0742610022;
      r4.w = r4.w * abs(r4.y) + -0.212114394;
      r4.w = r4.w * abs(r4.y) + 1.57072878;
      r5.w = r4.w * r4.z;
      r5.w = r5.w * -2 + 3.14159274;
      r4.y = cmp(r4.y < -r4.y);
      r4.y = r4.y ? r5.w : 0;
      r4.y = r4.w * r4.z + r4.y;
      r4.z = r4.x * -1.12524891 + 1.12839997;
      r8.xyw = float3(-0.0043633231,0.0043633231,0.0043633231) + r4.yyy;
      r4.y = 1 / r4.z;
      r8.xyw = r8.xyw * r4.yyy;
      r4.zw = float2(0.707106769,0.707106769) * r8.xy;
      r5.w = abs(r4.z) * abs(r4.z);
      r6.w = r5.w * abs(r4.z);
      r7.w = r6.w * abs(r4.z);
      r4.z = abs(r4.z) * 0.278393 + 1;
      r4.z = r5.w * 0.230388999 + r4.z;
      r4.z = r6.w * 0.000972000009 + r4.z;
      r4.z = r7.w * 0.0781079978 + r4.z;
      r4.z = r4.z * r4.z;
      r4.z = r4.z * r4.z;
      r4.z = 1 / r4.z;
      r4.xz = float2(1,1) + -r4.xz;
      r5.w = cmp(0 < r8.x);
      r6.w = cmp(r8.x < 0);
      r5.w = (int)-r5.w + (int)r6.w;
      r5.w = (int)r5.w;
      r4.z = r5.w * r4.z + 1;
      r4.z = 0.5 * r4.z;
      r5.w = r4.w * r4.w;
      r6.w = r5.w * r4.w;
      r4.w = r6.w * r4.w;
      r7.w = r8.w * 0.196853578 + 1;
      r5.w = r5.w * 0.230388999 + r7.w;
      r5.w = r6.w * 0.000972000009 + r5.w;
      r4.w = r4.w * 0.0781079978 + r5.w;
      r4.w = r4.w * r4.w;
      r4.w = r4.w * r4.w;
      r4.w = 1 / r4.w;
      r4.w = 2 + -r4.w;
      r4.z = r4.w * 0.5 + -r4.z;
      r8.xy = float2(-1.11072075,1.11072075) * r4.yy;
      r10.xy = r8.xy * r8.xy;
      r4.w = r10.x * -r8.x;
      r5.w = r4.w * -r8.x;
      r4.y = r4.y * 0.309216887 + 1;
      r8.xw = r10.xy * float2(0.230388999,0.230388999) + r4.yy;
      r4.y = r4.w * 0.000972000009 + r8.x;
      r4.y = r5.w * 0.0781079978 + r4.y;
      r4.y = r4.y * r4.y;
      r4.y = r4.y * r4.y;
      r4.y = 1 / r4.y;
      r4.y = 1 + -r4.y;
      r4.y = 1 + -r4.y;
      r4.y = 0.5 * r4.y;
      r4.w = r10.y * r8.y;
      r5.w = r4.w * r8.y;
      r4.w = r4.w * 0.000972000009 + r8.w;
      r4.w = r5.w * 0.0781079978 + r4.w;
      r4.w = r4.w * r4.w;
      r4.w = r4.w * r4.w;
      r4.w = 1 / r4.w;
      r4.w = 2 + -r4.w;
      r4.y = r4.w * 0.5 + -r4.y;
      r4.y = abs(r4.z) / r4.y;
      r4.y = min(1, r4.y);
      r4.y = r4.y * r4.y;
      r4.yz = float2(0.785398185,1.57079637) * r4.yx;
      r4.z = sin(r4.z);
      r4.w = -1 + r3.w;
      r4.z = r4.z * r4.w + 1;
      r4.x = r4.x * r4.x;
      r4.w = 0.5 * r4.x;
      r4.x = -r4.x * 0.5 + 1;
      r5.w = r3.w * r4.x + r4.w;
      r5.w = 1 / r5.w;
      r3.y = r3.y * r4.x + r4.w;
      r3.y = 1 / r3.y;
      r3.y = r5.w * r3.y;
      r3.y = 0.25 * r3.y;
      r3.y = min(1, r3.y);
      r3.y = r3.y * r4.z;
      r4.xzw = -r6.xyz * r3.xxx + -sun_direction.xyz;
      r3.x = dot(r4.xzw, r4.xzw);
      r3.x = rsqrt(r3.x);
      r4.xzw = r4.xzw * r3.xxx;
      r3.x = saturate(dot(r4.xzw, -sun_direction.xyz));
      r4.x = r3.x * -5.55472994 + -6.98316002;
      r3.x = r4.x * r3.x;
      r3.x = exp2(r3.x);
      r4.xzw = r5.xyz * r3.xxx + r0.yzw;
      r3.x = r4.y * r3.y;
      r4.xyz = r3.xxx * r4.xzw;
      r3.x = r3.w * -5.55472994 + -6.98316002;
      r3.x = r3.x * r3.w;
      r3.x = exp2(r3.x);
      r0.yzw = r5.xyz * r3.xxx + r0.yzw;
      r0.yzw = float3(1,1,1) + -r0.yzw;
      r3.x = r8.z * -1.12524891 + 1.12839997;
      r3.y = r3.x * r3.x;
      r5.xy = r3.xx * r3.xx + float2(0.330000013,0.0900000036);
      r3.x = r5.x + r5.x;
      r3.x = r3.y / r3.x;
      r3.x = 1 + -r3.x;
      r3.y = 0.449999988 * r3.y;
      r3.y = r3.y / r5.y;
      r3.z = max(-1, r3.z);
      r3.z = min(1, r3.z);
      r4.w = 1 + -abs(r3.z);
      r4.w = sqrt(r4.w);
      r5.x = abs(r3.z) * -0.0187292993 + 0.0742610022;
      r5.x = r5.x * abs(r3.z) + -0.212114394;
      r5.x = r5.x * abs(r3.z) + 1.57072878;
      r5.y = r5.x * r4.w;
      r5.y = r5.y * -2 + 3.14159274;
      r5.z = cmp(r3.z < -r3.z);
      r5.y = r5.z ? r5.y : 0;
      r4.w = r5.x * r4.w + r5.y;
      r5.x = 1 + -abs(r11.y);
      r5.x = sqrt(r5.x);
      r5.y = abs(r11.y) * -0.0187292993 + 0.0742610022;
      r5.y = r5.y * abs(r11.y) + -0.212114394;
      r5.y = r5.y * abs(r11.y) + 1.57072878;
      r5.z = r5.y * r5.x;
      r5.z = r5.z * -2 + 3.14159274;
      r5.w = cmp(r11.y < -r11.y);
      r5.z = r5.w ? r5.z : 0;
      r5.x = r5.y * r5.x + r5.z;
      r5.y = max(r5.x, r4.w);
      r5.z = min(r5.x, r4.w);
      r4.w = -r5.x + r4.w;
      r4.w = cos(r4.w);
      r4.w = max(0, r4.w);
      r3.y = r4.w * r3.y;
      r4.w = sin(r5.y);
      r3.y = r4.w * r3.y;
      sincos(r5.z, r5.x, r8.x);
      r4.w = r5.x / r8.x;
      r3.x = r3.y * r4.w + r3.x;
      r3.y = max(0, r3.z);
      r3.x = r3.x * r3.y;
      r0.yzw = sun_colour.xyz * r0.yzw;
      r0.yzw = r0.yzw * r2.xyz;
      r3.y = cmp(0.5 < g_camera_temp0.x);
      r3.x = r3.y ? r3.w : r3.x;
      r0.yzw = r3.xxx * r0.yzw;
      r0.yzw = float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * r0.yzw;
      r0.yzw = r4.xyz * sun_colour.xyz + r0.yzw;
      r2.xyz = r0.xxx * r0.yzw + r9.xyz;
    }
    r0.xy = float2(0.00200000009,0.00200000009) * v1.xz;
    r0.x = t_2d_noise.SampleLevel(s_2d_noise_s, r0.xy, 0).x;
    r0.x = g_force_fog.x * r0.x;
    r0.y = sqrt(r2.w);
    r0.z = min(g_height_fog_clear_distance, g_fog_clear_distance);
    r0.x = r0.x * 0.5 + r0.y;
    r0.w = g_force_fog.y + -g_force_fog.x;
    r0.x = -g_force_fog.x + r0.x;
    r0.w = 1 / r0.w;
    r0.x = saturate(r0.x * r0.w);
    r0.w = r0.x * -2 + 3;
    r0.x = r0.x * r0.x;
    r0.x = r0.w * r0.x;
    r0.w = 9.99999975e-005 + r0.z;
    r0.w = cmp(r0.w < r0.y);
    if (r0.w != 0) {
      r3.xyz = r7.xyz * r0.zzz + camera_position.xyz;
      r3.xyz = v1.xyz + -r3.xyz;
      r2.w = dot(r3.xyz, r3.xyz);
      r2.w = sqrt(r2.w);
      r3.w = g_campaign_density_scaling * r2.w;
      r4.x = max(0, v1.y);
      r4.xyzw = float4(-0.000206276105,-0.000206276105,-0.000206276105,-0.00120224594) * r4.xxxx;
      r4.xyzw = exp2(r4.xyzw);
      r4.xyzw = r4.xyzw * r3.wwww;
      r3.xyz = r3.xyz / r2.www;
      r2.w = -g_height_fog_clear_distance + r0.y;
      r2.w = max(0, r2.w);
      r5.xyz = float3(0.00999999978,-0.00999999978,0.00999999978) * time_in_sec;
      r5.xyz = v1.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r5.xyz;
      r3.w = t_volume_noise.SampleLevel(s_volume_noise_s, r5.xyz, 0).x;
      r3.w = -0.5 + r3.w;
      r5.y = -0.0799999982 * r3.w;
      r5.xz = float2(0,0);
      r3.xyz = r5.xyz + r3.xyz;
      r3.x = dot(r3.xyz, r3.xyz);
      r3.x = rsqrt(r3.x);
      r3.x = r3.y * r3.x;
      r3.yz = -float2(g_fog_height_top, g_fog_height_bottom) + camera_position.yy;
      r3.w = 10000 + camera_position.y;
      r3.yzw = r3.yzw / -r3.xxx;
      r5.xy = min(r3.yz, r3.zw);
      r3.y = max(r3.y, r3.z);
      r5.x = max(0, r5.x);
      r3.z = max(r3.z, r3.w);
      r3.w = max(0, r5.y);
      r3.yz = max(float2(0,0), r3.yz);
      r5.y = min(r5.x, r3.y);
      r5.z = max(r5.x, r2.w);
      r3.y = min(r5.z, r3.y);
      r5.z = cmp(r5.y != r3.y);
      r5.w = r5.y * r3.x + camera_position.y;
      r3.y = r3.y + -r5.x;
      r5.x = r5.y + -r5.x;
      r5.y = r5.x + -r3.y;
      r5.w = r5.w + r5.w;
      r3.y = r5.x + r3.y;
      r3.x = r3.x * r3.y + r5.w;
      r3.x = -g_fog_height_top * 2 + r3.x;
      r3.x = r5.y * r3.x;
      r3.y = g_fog_height_top + -g_fog_height_bottom;
      r3.y = r3.y + r3.y;
      r3.x = r3.x / r3.y;
      r3.x = r5.z ? r3.x : 0;
      r3.y = min(r3.w, r3.z);
      r2.w = max(r3.w, r2.w);
      r2.w = min(r2.w, r3.z);
      r3.z = cmp(r3.y != r2.w);
      r2.w = r2.w + -r3.w;
      r3.y = r3.y + -r3.w;
      r2.w = -r3.y + r2.w;
      r2.w = r3.x + r2.w;
      r2.w = r3.z ? r2.w : r3.x;
      r2.w = g_fog_density_height * r2.w;
      r3.x = -g_fog_clear_distance + r0.y;
      r3.x = max(0, r3.x);
      r3.x = g_fog_density_constant * r3.x;
      r2.w = max(r3.x, r2.w);
      r3.xyzw = float4(5.80000005e-006,1.35e-005,3.3100001e-005,2.20000002e-005) * r4.xyzw;
      r3.xyz = r3.xyz + r3.www;
      r3.xyz = r2.www * float3(0.0125000002,0.0125000002,0.0125000002) + r3.xyz;
      r3.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r3.xyz;
      r3.xyz = exp2(r3.xyz);
    } else {
      r3.xyz = float3(1,1,1);
    }
    r3.xyz = r0.xxx * -r3.xyz + r3.xyz;
    r3.xyz = float3(-1,-1,-1) + r3.xyz;
    r3.xyz = g_fog_lerp * r3.xyz + float3(1,1,1);
    r4.xyz = r6.xyz / r0.yyy;
    r2.w = dot(-r4.xyz, sun_direction.xyz);
    if (r0.w != 0) {
      r5.xyz = r7.xyz * r0.zzz + camera_position.xyz;
      r7.xyz = v1.xyz + -r5.xyz;
      r0.z = dot(r7.xyz, r7.xyz);
      r0.z = sqrt(r0.z);
      r6.yw = r5.xz * float2(0.00100000005,0.00100000005) + v0.xy;
      r6.yw = r4.xy * float2(0.100000001,0.100000001) + r6.yw;
      r6.yw = g_camera_jitter.xy + r6.yw;
      r6.yw = float2(0.103100002,0.103100002) * r6.yw;
      r6.yw = frac(r6.yw);
      r7.xyz = float3(19.1900005,19.1900005,19.1900005) + r6.wyy;
      r0.w = dot(r6.ywy, r7.xyz);
      r6.yw = r6.yw + r0.ww;
      r0.w = r6.y + r6.w;
      r0.w = r0.w * r6.y;
      r0.w = frac(r0.w);
      r0.w = frac(r0.w);
      r3.w = -5000 + r0.z;
      r3.w = saturate(0.000125000006 * r3.w);
      r4.w = r3.w * -2 + 3;
      r3.w = r3.w * r3.w;
      r3.w = r4.w * r3.w;
      r4.w = 0.25 + -r0.w;
      r0.w = r3.w * r4.w + r0.w;
      r6.yw = float2(0.0625,0.03125) * r0.zz;
      r3.w = cmp(0 < g_fog_noise);
      r7.xy = float2(0.00999999978,-0.00999999978) * time_in_sec;
      r8.xyz = float3(0,0,0);
      r9.xyz = float3(0,0,0);
      r7.zw = float2(0,0);
      r4.w = 0;
      r5.w = 0;
      while (true) {
        r8.w = cmp((int)r5.w >= 16);
        if (r8.w != 0) break;
        r8.w = (int)r5.w;
        r8.w = r8.w + r0.w;
        r8.w = r6.y * r8.w + r6.w;
        r8.w = min(r8.w, r0.z);
        r10.xyz = r4.xyz * r8.www + r5.xyz;
        r8.w = cmp(r8.w < 400);
        r8.w = r3.w ? r8.w : 0;
        if (r8.w != 0) {
          r10.xzw = time_in_sec * float3(4.4000001,-4.4000001,4.4000001) + r10.xyz;
          r10.xzw = float3(0.00999999978,0.00999999978,0.00999999978) * r10.xzw;
          r8.w = t_volume_noise.SampleLevel(s_volume_noise_s, r10.xzw, 0).x;
          r8.w = r8.w * 2 + -1;
          r8.w = r8.w * g_fog_noise + 1;
        } else {
          r8.w = 1;
        }
        r4.w = r8.w + r4.w;
        r8.w = 60000 + -r10.y;
        r9.w = r8.w * 0.5 + r10.y;
        r9.w = max(0, r9.w);
        r10.xz = float2(-0.000206276105,-0.00120224594) * r9.ww;
        r10.xz = exp2(r10.xz);
        r9.w = g_fog_density_mie * r10.z;
        r10.y = max(0, r10.y);
        r10.yz = float2(-0.000206276105,-0.00120224594) * r10.yy;
        r10.yz = exp2(r10.yz);
        r10.w = r10.y * r6.y;
        r7.z = r10.y * r6.y + r7.z;
        r10.y = g_fog_density_mie * r10.z;
        r10.z = r10.y * r6.y;
        r7.w = r10.y * r6.y + r7.w;
        r10.x = r10.x * r8.w + r7.z;
        r8.w = r9.w * r8.w + r7.w;
        r8.w = 2.20000002e-005 * r8.w;
        r11.xyz = r10.xxx * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r8.www;
        r11.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r11.xyz;
        r11.xyz = exp2(r11.xyz);
        r8.xyz = r11.xyz * r10.www + r8.xyz;
        r9.xyz = r11.xyz * r10.zzz + r9.xyz;
        r5.w = (int)r5.w + 1;
      }
      r0.z = r2.w * r2.w + 1;
      r0.w = 0.5 * r0.z;
      r5.xyz = r8.xyz * r0.www;
      r0.w = -r2.w * 1.51999998 + 1.5776;
      r0.w = log2(r0.w);
      r0.w = 1.5 * r0.w;
      r0.w = exp2(r0.w);
      r0.z = r0.z / r0.w;
      r0.z = 0.000978047145 * r0.z;
      r8.xyz = r9.xyz * r0.zzz;
      r8.xyz = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r8.xyz;
      r5.xyz = r5.xyz * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r8.xyz;
      r0.z = 0.0625 * r4.w;
      r0.w = -g_height_fog_clear_distance + r0.y;
      r0.w = max(0, r0.w);
      r7.xyz = v1.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r7.xyx;
      r3.w = t_volume_noise.SampleLevel(s_volume_noise_s, r7.xyz, 0).x;
      r3.w = -0.5 + r3.w;
      r7.y = -0.0799999982 * r3.w;
      r7.xz = float2(0,0);
      r4.xyz = r7.xyz + r4.xyz;
      r3.w = dot(r4.xyz, r4.xyz);
      r3.w = rsqrt(r3.w);
      r3.w = r4.y * r3.w;
      r4.x = r3.w * g_height_fog_clear_distance + camera_position.y;
      r4.y = cmp(r4.x >= g_fog_height_top);
      r4.z = cmp(0 < r3.w);
      r4.y = r4.z ? r4.y : 0;
      if (r4.y == 0) {
        r4.yz = -float2(g_fog_height_top, g_fog_height_bottom) + r4.xx;
        r4.w = 10000 + r4.x;
        r4.yzw = r4.yzw / -r3.www;
        r5.w = min(r4.y, r4.z);
        r4.y = max(r4.y, r4.z);
        r5.w = max(0, r5.w);
        r6.y = min(r4.z, r4.w);
        r4.z = max(r4.z, r4.w);
        r4.w = max(0, r6.y);
        r4.yz = max(float2(0,0), r4.yz);
        r6.y = g_fog_density_height * r0.z;
        r6.y = 0.0125000002 * r6.y;
        r6.w = min(r5.w, r4.y);
        r7.x = max(r5.w, r0.w);
        r7.x = min(r7.x, r4.y);
        r7.y = cmp(r6.w != r7.x);
        r4.x = r5.w * r3.w + r4.x;
        r7.x = r7.x + -r5.w;
        r7.z = r6.w + -r5.w;
        r7.w = g_fog_height_top + -g_fog_height_bottom;
        r7.w = r7.w + r7.w;
        r4.x = r4.x + r4.x;
        r8.x = r7.z * r3.w + r4.x;
        r8.x = -g_fog_height_top * 2 + r8.x;
        r7.z = r8.x * r7.z;
        r7.z = r7.z * r6.y;
        r7.z = r7.z / r7.w;
        r7.z = 1.44269502 * r7.z;
        r7.z = exp2(r7.z);
        r8.x = r7.x * r3.w + r4.x;
        r8.x = -g_fog_height_top * 2 + r8.x;
        r7.x = r8.x * r7.x;
        r7.x = r7.x * r6.y;
        r7.x = r7.x / r7.w;
        r7.x = 1.44269502 * r7.x;
        r7.x = exp2(r7.x);
        r7.x = r7.z + -r7.x;
        r7.z = min(r4.w, r4.z);
        r8.x = max(r6.w, r4.w);
        r8.x = min(r8.x, r4.z);
        r8.y = cmp(r7.z != r8.x);
        r8.x = r8.x + -r4.w;
        r8.x = -r8.x * r6.y;
        r8.x = 1.44269502 * r8.x;
        r8.x = exp2(r8.x);
        r8.x = r8.x * r7.x;
        r7.x = r8.y ? r8.x : r7.x;
        r7.x = r7.y ? r7.x : 0;
        r7.y = max(r4.w, r0.w);
        r4.z = min(r7.y, r4.z);
        r7.y = cmp(r7.z != r4.z);
        r8.x = r4.z + -r4.w;
        r4.w = r7.z + -r4.w;
        r4.w = -r4.w * r6.y;
        r4.w = 1.44269502 * r4.w;
        r4.w = exp2(r4.w);
        r7.z = -r8.x * r6.y;
        r7.z = 1.44269502 * r7.z;
        r7.z = exp2(r7.z);
        r4.w = -r7.z + r4.w;
        r4.z = max(r4.z, r5.w);
        r4.y = min(r4.z, r4.y);
        r4.z = cmp(r6.w != r4.y);
        r4.y = r4.y + -r5.w;
        r3.w = r4.y * r3.w + r4.x;
        r3.w = -g_fog_height_top * 2 + r3.w;
        r3.w = r4.y * r3.w;
        r3.w = r3.w * r6.y;
        r3.w = r3.w / r7.w;
        r3.w = 1.44269502 * r3.w;
        r3.w = exp2(r3.w);
        r3.w = r4.w * r3.w;
        r3.w = r4.z ? r3.w : r4.w;
        r3.w = r7.x + r3.w;
        r4.xyz = r7.yyy ? r3.www : r7.xxx;
      } else {
        r4.xyz = float3(0,0,0);
      }
      r3.w = 1 / g_clear_distance_fade_out;
      r0.w = saturate(r3.w * r0.w);
      r3.w = r0.w * -2 + 3;
      r0.w = r0.w * r0.w;
      r0.w = r3.w * r0.w;
      r4.xyz = r4.xyz * r0.www;
      r0.w = -g_fog_clear_distance + r0.y;
      r0.w = max(0, r0.w);
      r3.w = cmp(0.00999999978 < g_clear_distance_fade_out);
      r4.w = cmp(0.00999999978 < r0.w);
      r3.w = r3.w ? r4.w : 0;
      r4.w = min(g_clear_distance_fade_out, r0.w);
      r5.w = r4.w * r4.w;
      r6.y = g_clear_distance_fade_out + g_clear_distance_fade_out;
      r5.w = r5.w / r6.y;
      r4.w = -r4.w + r0.w;
      r4.w = r5.w + r4.w;
      r4.w = r4.w / r0.w;
      r3.w = r3.w ? r4.w : 1;
      r0.z = g_fog_density_constant * r0.z;
      r0.z = r0.z * r3.w;
      r0.z = r0.z * r0.w;
      r0.z = 0.0180336889 * r0.z;
      r0.w = exp2(r0.z);
      r0.z = exp2(-r0.z);
      r3.w = r0.w + r0.z;
      r0.z = r0.w + -r0.z;
      r0.z = 0.5 * r0.z;
      r0.z = saturate(r3.w * 0.5 + -r0.z);
      r0.z = 1 + -r0.z;
      r4.xyz = max(r4.xyz, r0.zzz);
    } else {
      r4.xyz = float3(0,0,0);
      r5.xyz = float3(0,0,0);
    }
    r0.xzw = max(r4.xyz, r0.xxx);
    r2.w = r2.w * r2.w + 1;
    r2.w = 0.5 * r2.w;
    r4.xyz = r0.xzw * r2.www + r5.xyz;
    r5.xyz = r0.xzw * r2.www + -r4.xyz;
    r0.xzw = r0.xzw * r5.xyz + r4.xyz;
    r0.xzw = g_fog_colour.xyz * r0.xzw;
    if (g_use_spherical_harmonics != 0) {
      r0.y = 1 / r0.y;
      r0.y = min(1, r0.y);
      r4.xy = r0.yy * r6.xz + camera_position.xz;
      r4.xy = -g_probe_grid_area.xy + r4.xy;
      r4.zw = g_probe_grid_area.zw + -g_probe_grid_area.xy;
      r4.xy = r4.xy / r4.zw;
      r0.y = g_probe_grid_size;
      r4.xy = r4.xy * r0.yy + float2(-0.5,-0.5);
      r0.y = -1 + r0.y;
      r4.xy = max(float2(0,0), r4.xy);
      r4.xy = min(r4.xy, r0.yy);
      r4.zw = floor(r4.xy);
      r4.xy = frac(r4.xy);
      r4.zw = (int2)r4.zw;
      r5.xy = (int2)r4.zw * int2(3,3);
      r0.y = mad(g_probe_grid_size, 3, -3);
      r5.xy = min((uint2)r5.xy, (uint2)r0.yy);
      r4.zw = mad((int2)r4.zw, int2(3,3), int2(3,3));
      r4.zw = min((uint2)r4.zw, (uint2)r0.yy);
      r0.y = round(g_spherical_harmonic_terms);
      r0.y = (int)r0.y;
      r6.w = 0;
      r7.xyz = float3(0,0,0);
      r2.w = 0;
      while (true) {
        r3.w = cmp((int)r2.w >= (int)r0.y);
        if (r3.w != 0) break;
        r3.w = (uint)r2.w % 3;
        r6.y = (int)r3.w + (int)r5.x;
        r6.z = (int)r5.y + (int)icb[r2.w+0].y;
        r8.xyz = t_world_spherical_harmonics.Load(r6.yzw).xyz;
        r6.x = (int)r3.w + (int)r4.z;
        r9.xyz = t_world_spherical_harmonics.Load(r6.xzw).xyz;
        r10.y = (int)r4.w + (int)icb[r2.w+0].y;
        r10.xzw = r6.yww;
        r11.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
        r10.xzw = r6.xww;
        r6.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
        r9.xyz = r9.xyz + -r8.xyz;
        r8.xyz = r4.xxx * r9.xyz + r8.xyz;
        r6.xyz = r6.xyz + -r11.xyz;
        r6.xyz = r4.xxx * r6.xyz + r11.xyz;
        r6.xyz = r6.xyz + -r8.xyz;
        r6.xyz = r4.yyy * r6.xyz + r8.xyz;
        r7.xyz = r6.xyz * icb[r2.w+0].zzz + r7.xyz;
        r2.w = (int)r2.w + 1;
      }
      r6.xyz = max(float3(0,0,0), r7.xyz);
      r2.w = cmp(g_specular_probe_lerp < 0.999000013);
      if (r2.w != 0) {
        r7.w = 0;
        r8.xyz = float3(0,0,0);
        r3.w = 0;
        while (true) {
          r5.z = cmp((int)r3.w >= (int)r0.y);
          if (r5.z != 0) break;
          r5.z = (uint)r3.w % 3;
          r7.y = (int)r5.z + (int)r5.x;
          r7.z = (int)r5.y + (int)icb[r3.w+0].y;
          r9.xyz = t_prev_world_spherical_harmonics.Load(r7.yzw).xyz;
          r7.x = (int)r4.z + (int)r5.z;
          r10.xyz = t_prev_world_spherical_harmonics.Load(r7.xzw).xyz;
          r11.y = (int)r4.w + (int)icb[r3.w+0].y;
          r11.xzw = r7.yww;
          r12.xyz = t_prev_world_spherical_harmonics.Load(r11.xyz).xyz;
          r11.xzw = r7.xww;
          r7.xyz = t_prev_world_spherical_harmonics.Load(r11.xyz).xyz;
          r10.xyz = r10.xyz + -r9.xyz;
          r9.xyz = r4.xxx * r10.xyz + r9.xyz;
          r7.xyz = r7.xyz + -r12.xyz;
          r7.xyz = r4.xxx * r7.xyz + r12.xyz;
          r7.xyz = r7.xyz + -r9.xyz;
          r7.xyz = r4.yyy * r7.xyz + r9.xyz;
          r8.xyz = r7.xyz * icb[r3.w+0].zzz + r8.xyz;
          r3.w = (int)r3.w + 1;
        }
        r4.xyz = max(float3(0,0,0), r8.xyz);
        r5.xyz = r6.xyz + -r4.xyz;
        r4.xyz = g_specular_probe_lerp * r5.xyz + r4.xyz;
      }
      r4.xyz = r2.www ? r4.xyz : r6.xyz;
      r4.xyz = g_ambient_fudge_factor * r4.xyz;
    } else {
      r4.xyz = g_ambient_fudge_factor * ambient_cube_tb[0].xyz;
    }
    r4.xyz = sun_colour.xyz * float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) + r4.xyz;
    r0.xyz = r4.xyz * r0.xzw;
    r0.xyz = g_fog_lerp * r0.xyz;
    r1.xyz = r3.xyz * r2.xyz + r0.xyz;
  }
  o0.xyzw = saturate(r1.xyzw);  // RenoDX
  return;
}
