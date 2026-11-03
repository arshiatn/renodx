// variantweightedvtf.hlsl ps30_main_ui (decompiled): the 3D figures in the menus (clan leaders, generals). Their target is the UI's
// (8-bit in the game: every output clamped to 0-1), upgraded to 16-bit by the mod: its alpha of 2 (lit pixels) and colour kept above 1, the figure turned
// see-through over the menu (ATN debug22). RenoDX: the output clamped as the 8-bit target did.

cbuffer colorimetry_VS_PS : register(b0)
{
  float g_brightness : packoffset(c0) = {1.20000005};
  float g_gamma_output : packoffset(c0.y) = {2.20000005};
  float g_inv_gamma_output : packoffset(c0.z) = {0.454545468};
  int g_hdr_display : packoffset(c0.w) = {0};
  float g_hdr_peak_nits : packoffset(c1);
  float g_hdr_scene_nits : packoffset(c1.y);
  float g_hdr_ui_nits : packoffset(c1.z);
}

cbuffer camera_VS_PS : register(b1)
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

cbuffer environment_PS : register(b2)
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
  float g_fog_shadow_strength : packoffset(c4.z);
  float g_fog_blur_offset : packoffset(c4.w);
  float g_fog_blur_strength : packoffset(c5);
  float2 g_force_fog : packoffset(c5.y);
}

cbuffer variant_faction_colours_PS : register(b5)
{
  float4 faction_colours[6] : packoffset(c0);
}

cbuffer variant_materials_PS : register(b6)
{
  float3 materials[4] : packoffset(c0);
  float4 faction_colour_blend : packoffset(c4);
  float enable_specular : packoffset(c5);
  float2 specular : packoffset(c5.y);
}

cbuffer variant_environment_PS : register(b7)
{
  float wet : packoffset(c0);
}

cbuffer UI_character_pointlight_PS : register(b8)
{
  float3 g_point_light_color : packoffset(c0);
  float g_point_light_radius : packoffset(c0.w);
  float3 g_point_light_position : packoffset(c1);
  float g_point_light_intensity : packoffset(c1.w);
}

SamplerState s_environment_array_s : register(s0);
SamplerState s_ibl_brdf_lut_s : register(s1);
SamplerState s_cloud_shadow_s : register(s2);
SamplerState s_volume_noise_s : register(s3);
SamplerState s_2d_noise_s : register(s4);
SamplerState s_diffuse_map_s : register(s5);
SamplerState s_colour_mask_map_s : register(s6);
SamplerState s_normal_map_s : register(s7);
SamplerState s_gloss_map_s : register(s8);
Texture2D<float4> s_diffuse_map : register(t0);
Texture2D<float4> s_colour_mask_map : register(t1);
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
Texture2D<float4> t_dither_map : register(t12);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD8,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float4 v8 : TEXCOORD6,
  float4 v9 : TEXCOORD7,
  float2 v10 : COLOR0,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = s_diffuse_map.Sample(s_diffuse_map_s, v3.xy).xyzw;
  r1.xy = (uint2)v0.xy;
  r1.xy = (int2)r1.xy & int2(7,7);
  r1.zw = float2(0,0);
  r1.x = t_dither_map.Load(r1.xyz).x;
  r1.x = v6.z + -r1.x;
  r1.x = cmp(r1.x < 0);
  if (r1.x != 0) discard;
  r0.w = -0.501960814 + r0.w;
  r0.w = cmp(r0.w < 0);
  if (r0.w != 0) discard;
  r1.xyz = s_colour_mask_map.Sample(s_colour_mask_map_s, v6.xy).xyz;
  r2.xyz = faction_colour_blend.xyz * r1.xyz;
  r3.xyz = faction_colours[0].xyz * r0.xyz + -r0.xyz;
  r0.xyz = r2.xxx * r3.xyz + r0.xyz;
  r3.xyz = faction_colours[1].xyz * r0.xyz + -r0.xyz;
  r0.xyz = r2.yyy * r3.xyz + r0.xyz;
  r2.xyw = faction_colours[2].xyz * r0.xyz + -r0.xyz;
  r0.xyz = r2.zzz * r2.xyw + r0.xyz;
  r2.x = v4.w;
  r2.y = v5.w;
  r2.xyz = s_gloss_map.Sample(s_gloss_map_s, r2.xy).xyw;
  r1.xyz = cmp(r1.xyz >= float3(0.5,0.5,0.5));
  r1.xyz = r1.xyz ? float3(1,1,1) : 0;
  r0.w = -materials[3].z + materials[0].z;
  r0.w = r1.x * r0.w + materials[3].z;
  r1.x = materials[1].z + -r0.w;
  r0.w = r1.y * r1.x + r0.w;
  r1.x = materials[2].z + -r0.w;
  r1.x = r1.z * r1.x + r0.w;
  r0.w = cmp(r1.x < 0);
  r1.w = dot(v5.xyz, v5.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = v5.xyz * r1.www;
  r1.w = dot(v8.xyz, v8.xyz);
  r1.w = rsqrt(r1.w);
  r4.xyz = v8.xyz * r1.www;
  r1.w = dot(v4.xyz, v4.xyz);
  r1.w = rsqrt(r1.w);
  r5.xyz = v4.xyz * r1.www;
  if (r0.w == 0) {
    r6.xyz = s_normal_map.Sample(s_normal_map_s, v3.zw).xzw;
    r6.xyz = r6.xzy * float3(2,2,2) + float3(-1,-1,-1);
  } else {
    r7.xy = s_normal_map.Sample(s_normal_map_s, v3.zw).yw;
    r7.xy = float2(0.00196078443,0.00196078443) + r7.yx;
    r6.xy = r7.xy * float2(2,2) + float2(-1,-1);
    r1.w = dot(r6.xy, r6.xy);
    r1.w = 1 + -r1.w;
    r1.w = max(0, r1.w);
    r6.z = sqrt(r1.w);
  }
  r4.xyz = r6.yyy * r4.xyz;
  r3.xyz = r6.xxx * r3.xyz + r4.xyz;
  r3.xyz = r6.zzz * r5.xyz + r3.xyz;
  r1.w = dot(r3.xyz, r3.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = r3.xyz * r1.www;
  r1.w = cmp(wet != 0.000000);
  r2.w = -1 + wet;
  r2.w = enable_specular * r2.w + 1;
  r1.w = r1.w ? r2.w : 1;
  r1.y = 1 + -r2.y;
  r1.z = 1;
  r1.xyz = r0.www ? r2.xyz : r1.xyz;
  r0.xyz = r1.www * r0.xyz;
  r0.xyz = saturate(v6.www * r0.xyz);
  r2.xyz = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r0.xyz;
  r2.xyz = r1.xxx * r2.xyz + float3(0.0399999991,0.0399999991,0.0399999991);
  r0.w = 1 + -r1.x;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = float3(0.959999979,0.959999979,0.959999979) * r0.xyz;
  r4.xyz = float3(1,1,1) + -r2.xyz;
  r5.xyz = max(float3(0.00100000005,0.00100000005,0.00100000005), r4.xyz);
  r0.xyz = r0.xyz / r5.xyz;
  r0.w = -r1.y * r1.w + 1;
  r0.w = max(0, r0.w);
  r5.zw = min(float2(1,0.200000003), r0.ww);
  r1.xyw = -camera_position.xyz + v1.xyz;
  r0.w = dot(r1.xyw, r1.xyw);
  r2.w = rsqrt(r0.w);
  r6.xyz = r2.www * r1.xyw;
  r3.w = cmp(g_cloud_shadow_scale != 0.000000);
  if (r3.w != 0) {
    r7.xy = v1.xz / g_cloud_shadow_scale;
    r3.w = g_cloud_shadow_speed / g_cloud_shadow_scale;
    r7.zw = g_cloud_shadow_direction.xy * r3.ww;
    r7.xy = r7.zw * time_in_sec + r7.xy;
    r7.xyzw = t_cloud_shadow.SampleLevel(s_cloud_shadow_s, r7.xy, 0).xyzw;
    r7.xyzw = float4(1,1,1,1) + -r7.xyzw;
    r3.w = 4 * g_cloud_shadow_lerp;
    r3.w = min(1, abs(r3.w));
    r3.w = 1 + -r3.w;
    r8.xyzw = g_cloud_shadow_lerp * float4(4,4,4,4) + float4(-1,-2,-3,-4);
    r8.xyzw = min(float4(1,1,1,1), abs(r8.xyzw));
    r8.xyzw = float4(1,1,1,1) + -r8.xyzw;
    r3.w = r7.x * r8.x + r3.w;
    r3.w = r7.y * r8.y + r3.w;
    r3.w = r7.z * r8.z + r3.w;
    r3.w = r7.w * r8.w + r3.w;
  } else {
    r3.w = 1;
  }
  r3.w = min(1, r3.w);
  r4.w = dot(r6.xyz, r3.xyz);
  r6.w = r4.w + r4.w;
  r7.xyz = r3.xyz * -r6.www + r6.xyz;
  if (g_use_spherical_harmonics != 0) {
    t_world_spherical_harmonics.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r8.xy = uiDest.xy;
    uiDest.xy = (uint2)r8.xy / int2(3,3);
    r8.xy = uiDest.xy;
    r8.xy = (uint2)r8.xy;
    r8.xy = float2(0.5,0.5) * r8.xy;
    r8.xy = trunc(r8.xy);
    r9.xyzw = r3.xzyz * r3.zyxz;
    r6.w = r9.w * 3 + -1;
    r8.z = r3.y * r3.y;
    r8.z = r3.x * r3.x + -r8.z;
    r8.w = round(g_spherical_harmonic_terms);
    r8.w = (int)r8.w;
    r10.zw = float2(0,0);
    r11.xyz = float3(0,0,0);
    r9.w = 0;
    while (true) {
      r11.w = cmp((int)r9.w >= (int)r8.w);
      if (r11.w != 0) break;
      r11.w = (uint)r9.w % 3;
      r11.w = (int)r11.w;
      r11.w = r8.x * 3 + r11.w;
      r12.x = (int)icb[r9.w+0].y;
      r12.x = r8.y * 3 + r12.x;
      r10.x = (int)r11.w;
      r10.y = (int)r12.x;
      r12.xyz = t_world_spherical_harmonics.Load(r10.xyz).xyz;
      r13.xyzw = cmp((int4)r9.wwww == int4(0,1,2,3));
      r14.xyzw = cmp((int4)r9.wwww == int4(4,5,6,7));
      r10.x = cmp((int)r9.w == 8);
      r13.xyzw = r13.xyzw ? float4(1,1,1,1) : 0;
      r14.xyzw = r14.xyzw ? float4(1,1,1,1) : 0;
      r10.x = r10.x ? 1.000000 : 0;
      r10.y = r13.y * r3.x + r13.x;
      r10.y = r13.z * r3.y + r10.y;
      r10.y = r13.w * r3.z + r10.y;
      r10.y = r14.x * r9.x + r10.y;
      r10.y = r14.y * r9.y + r10.y;
      r10.y = r14.z * r9.z + r10.y;
      r10.y = r14.w * r6.w + r10.y;
      r10.x = r10.x * r8.z + r10.y;
      r11.xyz = r12.xyz * r10.xxx + r11.xyz;
      r9.w = (int)r9.w + 1;
    }
    r8.xyz = max(float3(0,0,0), r11.xyz);
    r8.xyz = g_ambient_fudge_factor * r8.xyz;
  } else {
    r9.xyz = cmp(r3.xyz < float3(0,0,0));
    r10.xyz = r9.xxx ? ambient_cube_lr[1].xyz : ambient_cube_lr[0].xyz;
    r9.xyw = r9.yyy ? ambient_cube_tb[1].xyz : ambient_cube_tb[0].xyz;
    r11.xyz = r9.zzz ? ambient_cube_fb[1].xyz : ambient_cube_fb[0].xyz;
    r12.xyz = r3.xyz * r3.xyz;
    r9.xyz = r12.yyy * r9.xyw;
    r9.xyz = r12.xxx * r10.xyz + r9.xyz;
    r9.xyz = r12.zzz * r11.xyz + r9.xyz;
    r8.xyz = g_ambient_fudge_factor * r9.xyz;
  }
  r8.xyz = r8.xyz * r0.xyz;
  r8.xyz = r8.xyz * r4.xyz;
  r1.z = min(1, r1.z);
  r6.w = 1 + -r5.z;
  r8.w = dot(r3.xyz, -r6.xyz);
  r5.xy = max(float2(0,-1), r8.ww);
  r9.xy = min(float2(1,1), r5.xy);
  r5.y = r9.x * -5.55472994 + -6.98316002;
  r5.y = r5.y * r9.x;
  r5.y = exp2(r5.y);
  r8.w = 1 + -r6.w;
  r10.xyz = max(r8.www, r2.xyz);
  r10.xyz = r10.xyz + -r2.xyz;
  r10.xyz = r10.xyz * r5.yyy + r2.xyz;
  r5.xy = t_ibl_brdf_lut.SampleLevel(s_ibl_brdf_lut_s, r5.xz, 0).xy;
  r10.xyz = r10.xyz * r5.xxx + r5.yyy;
  r5.x = 8 * r6.w;
  r5.x = min(7, r5.x);
  r9.zw = -g_probe_grid_area.xy + v1.xz;
  r11.xy = g_probe_grid_area.zw + -g_probe_grid_area.xy;
  r9.zw = r9.zw / r11.xy;
  r5.y = g_probe_grid_size;
  r9.zw = r9.zw * r5.yy + float2(-0.5,-0.5);
  r6.w = -1 + r5.y;
  r9.zw = max(float2(0,0), r9.zw);
  r9.zw = min(r9.zw, r6.ww);
  r11.zw = floor(r9.zw);
  r9.zw = frac(r9.zw);
  r7.w = r11.w * r5.y + r11.z;
  r12.xyz = t_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, r5.x).xyz;
  r13.xyzw = float4(1,0,0,1) + r11.zwzw;
  r13.xw = r13.yw * r5.yy + r13.xz;
  r14.xyz = r7.xyz;
  r14.w = r13.x;
  r15.xyz = t_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r5.x).xyz;
  r13.xyz = r14.xyz;
  r16.xyz = t_environment_array.SampleLevel(s_environment_array_s, r13.xyzw, r5.x).xyz;
  r11.zw = float2(1,1) + r11.zw;
  r17.w = r11.w * r5.y + r11.z;
  r17.xyz = r13.xyz;
  r18.xyz = t_environment_array.SampleLevel(s_environment_array_s, r17.xyzw, r5.x).xyz;
  r15.xyz = r15.xyz + -r12.xyz;
  r12.xyz = r9.zzz * r15.xyz + r12.xyz;
  r15.xyz = r18.xyz + -r16.xyz;
  r15.xyz = r9.zzz * r15.xyz + r16.xyz;
  r15.xyz = r15.xyz + -r12.xyz;
  r12.xyz = r9.www * r15.xyz + r12.xyz;
  r8.w = cmp(g_specular_probe_lerp < 0.999000013);
  if (r8.w != 0) {
    r7.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r7.xyzw, r5.x).xyz;
    r15.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r14.xyzw, r5.x).xyz;
    r13.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r13.xyzw, r5.x).xyz;
    r16.xyz = t_prev_environment_array.SampleLevel(s_environment_array_s, r17.xyzw, r5.x).xyz;
    r15.xyz = r15.xyz + -r7.xyz;
    r7.xyz = r9.zzz * r15.xyz + r7.xyz;
    r15.xyz = r16.xyz + -r13.xyz;
    r13.xyz = r9.zzz * r15.xyz + r13.xyz;
    r13.xyz = r13.xyz + -r7.xyz;
    r7.xyz = r9.www * r13.xyz + r7.xyz;
    r13.xyz = r12.xyz + -r7.xyz;
    r7.xyz = g_specular_probe_lerp * r13.xyz + r7.xyz;
  }
  r7.xyz = r8.www ? r7.xyz : r12.xyz;
  r7.xyz = r7.xyz * r10.xyz;
  r5.x = 1 + -r1.z;
  r5.x = r5.z * r5.x + r1.z;
  r7.xyz = r5.xxx * r7.xyz;
  r7.xyz = r7.xyz * r1.zzz;
  r7.xyz = r8.xyz * r1.zzz + r7.xyz;
  r1.z = dot(r3.xyz, -sun_direction.xyz);
  r5.x = saturate(r1.z);
  r7.w = cmp(0 < sun_specular);
  r5.w = r7.w ? r5.z : r5.w;
  r7.w = dot(-sun_direction.xyz, r14.xyz);
  r7.w = max(-1, r7.w);
  r7.w = min(1, r7.w);
  r8.x = 1 + -abs(r7.w);
  r8.x = sqrt(r8.x);
  r8.y = abs(r7.w) * -0.0187292993 + 0.0742610022;
  r8.y = r8.y * abs(r7.w) + -0.212114394;
  r8.y = r8.y * abs(r7.w) + 1.57072878;
  r8.z = r8.y * r8.x;
  r8.z = r8.z * -2 + 3.14159274;
  r7.w = cmp(r7.w < -r7.w);
  r7.w = r7.w ? r8.z : 0;
  r7.w = r8.y * r8.x + r7.w;
  r8.x = r5.w * -1.12524891 + 1.12839997;
  r10.xyz = float3(-0.0043633231,0.0043633231,0.0043633231) + r7.www;
  r7.w = 1 / r8.x;
  r8.xyz = r10.xyz * r7.www;
  r9.zw = float2(0.707106769,0.707106769) * r8.xy;
  r8.y = abs(r9.z) * abs(r9.z);
  r10.x = r8.y * abs(r9.z);
  r10.y = r10.x * abs(r9.z);
  r9.z = abs(r9.z) * 0.278393 + 1;
  r8.y = r8.y * 0.230388999 + r9.z;
  r8.y = r10.x * 0.000972000009 + r8.y;
  r8.y = r10.y * 0.0781079978 + r8.y;
  r8.y = r8.y * r8.y;
  r8.y = r8.y * r8.y;
  r8.y = 1 / r8.y;
  r8.y = 1 + -r8.y;
  r9.z = cmp(0 < r8.x);
  r8.x = cmp(r8.x < 0);
  r8.x = (int)-r9.z + (int)r8.x;
  r8.x = (int)r8.x;
  r8.x = r8.x * r8.y + 1;
  r8.x = 0.5 * r8.x;
  r8.y = r9.w * r9.w;
  r9.z = r8.y * r9.w;
  r9.w = r9.z * r9.w;
  r8.z = r8.z * 0.196853578 + 1;
  r8.y = r8.y * 0.230388999 + r8.z;
  r8.y = r9.z * 0.000972000009 + r8.y;
  r8.y = r9.w * 0.0781079978 + r8.y;
  r8.y = r8.y * r8.y;
  r8.y = r8.y * r8.y;
  r8.y = 1 / r8.y;
  r8.y = 2 + -r8.y;
  r8.x = r8.y * 0.5 + -r8.x;
  r8.yz = float2(-1.11072075,1.11072075) * r7.ww;
  r9.zw = r8.yz * r8.yz;
  r10.x = r9.z * -r8.y;
  r8.y = r10.x * -r8.y;
  r7.w = r7.w * 0.309216887 + 1;
  r10.yz = r9.zw * float2(0.230388999,0.230388999) + r7.ww;
  r7.w = r10.x * 0.000972000009 + r10.y;
  r7.w = r8.y * 0.0781079978 + r7.w;
  r7.w = r7.w * r7.w;
  r7.w = r7.w * r7.w;
  r7.w = 1 / r7.w;
  r7.w = 1 + -r7.w;
  r7.w = 1 + -r7.w;
  r7.w = 0.5 * r7.w;
  r8.y = r9.w * r8.z;
  r8.z = r8.y * r8.z;
  r8.y = r8.y * 0.000972000009 + r10.z;
  r8.y = r8.z * 0.0781079978 + r8.y;
  r8.y = r8.y * r8.y;
  r8.y = r8.y * r8.y;
  r8.y = 1 / r8.y;
  r8.y = 2 + -r8.y;
  r7.w = r8.y * 0.5 + -r7.w;
  r7.w = abs(r8.x) / r7.w;
  r7.w = min(1, r7.w);
  r7.w = r7.w * r7.w;
  r7.w = 0.785398185 * r7.w;
  r5.w = 1 + -r5.w;
  r8.x = 1.57079637 * r5.w;
  r8.x = sin(r8.x);
  r8.y = -1 + r5.x;
  r8.y = r8.x * r8.y + 1;
  r8.z = saturate(r4.w);
  r5.w = r5.w * r5.w;
  r9.z = 0.5 * r5.w;
  r9.w = -r5.w * 0.5 + 1;
  r10.x = r5.x * r9.w + r9.z;
  r10.x = 1 / r10.x;
  r8.z = r8.z * r9.w + r9.z;
  r8.z = 1 / r8.z;
  r8.z = r10.x * r8.z;
  r8.z = 0.25 * r8.z;
  r8.z = min(1, r8.z);
  r8.y = r8.z * r8.y;
  r10.xyz = -r1.xyw * r2.www + -sun_direction.xyz;
  r8.z = dot(r10.xyz, r10.xyz);
  r8.z = rsqrt(r8.z);
  r10.xyz = r10.xyz * r8.zzz;
  r8.z = saturate(dot(r10.xyz, -sun_direction.xyz));
  r10.x = r8.z * -5.55472994 + -6.98316002;
  r8.z = r10.x * r8.z;
  r8.z = exp2(r8.z);
  r10.xyz = r4.xyz * r8.zzz + r2.xyz;
  r7.w = r8.y * r7.w;
  r10.xyz = r7.www * r10.xyz;
  r10.xyz = sun_colour.xyz * r10.xyz;
  r7.w = r5.x * -5.55472994 + -6.98316002;
  r7.w = r7.w * r5.x;
  r7.w = exp2(r7.w);
  r12.xyz = r4.xyz * r7.www + r2.xyz;
  r12.xyz = float3(1,1,1) + -r12.xyz;
  r5.z = r5.z * -1.12524891 + 1.12839997;
  r7.w = r5.z * r5.z;
  r8.yz = r5.zz * r5.zz + float2(0.330000013,0.0900000036);
  r5.z = r8.y + r8.y;
  r5.z = r7.w / r5.z;
  r5.z = 1 + -r5.z;
  r7.w = 0.449999988 * r7.w;
  r7.w = r7.w / r8.z;
  r1.z = max(-1, r1.z);
  r1.z = min(1, r1.z);
  r8.y = 1 + -abs(r1.z);
  r8.y = sqrt(r8.y);
  r8.z = abs(r1.z) * -0.0187292993 + 0.0742610022;
  r8.z = r8.z * abs(r1.z) + -0.212114394;
  r8.z = r8.z * abs(r1.z) + 1.57072878;
  r10.w = r8.z * r8.y;
  r10.w = r10.w * -2 + 3.14159274;
  r11.z = cmp(r1.z < -r1.z);
  r10.w = r11.z ? r10.w : 0;
  r8.y = r8.z * r8.y + r10.w;
  r8.z = 1 + -abs(r9.y);
  r8.z = sqrt(r8.z);
  r10.w = abs(r9.y) * -0.0187292993 + 0.0742610022;
  r10.w = r10.w * abs(r9.y) + -0.212114394;
  r10.w = r10.w * abs(r9.y) + 1.57072878;
  r11.z = r10.w * r8.z;
  r11.z = r11.z * -2 + 3.14159274;
  r9.y = cmp(r9.y < -r9.y);
  r9.y = r9.y ? r11.z : 0;
  r8.z = r10.w * r8.z + r9.y;
  r9.y = max(r8.z, r8.y);
  r10.w = min(r8.z, r8.y);
  r8.y = r8.y + -r8.z;
  r8.y = cos(r8.y);
  r8.y = max(0, r8.y);
  r8.y = r8.y * r7.w;
  r8.z = sin(r9.y);
  r8.y = r8.y * r8.z;
  sincos(r10.w, r13.x, r14.x);
  r8.z = r13.x / r14.x;
  r8.y = r8.y * r8.z + r5.z;
  r1.z = max(0, r1.z);
  r1.z = r8.y * r1.z;
  r12.xyz = sun_colour.xyz * r12.xyz;
  r12.xyz = r12.xyz * r0.xyz;
  r8.y = cmp(0.5 < g_camera_temp0.x);
  r1.z = r8.y ? r5.x : r1.z;
  r12.xyz = r12.xyz * r1.zzz;
  r12.xyz = float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) * r12.xyz;
  r10.xyz = r10.xyz * g_light_scale_for_units + r12.xyz;
  r7.xyz = r3.www * r10.xyz + r7.xyz;
  r10.xy = float2(0.00200000009,0.00200000009) * v1.xz;
  r1.z = t_2d_noise.SampleLevel(s_2d_noise_s, r10.xy, 0).x;
  r1.z = g_force_fog.x * r1.z;
  r0.w = sqrt(r0.w);
  r3.w = min(g_height_fog_clear_distance, g_fog_clear_distance);
  r1.z = r1.z * 0.5 + r0.w;
  r5.x = g_force_fog.y + -g_force_fog.x;
  r1.z = -g_force_fog.x + r1.z;
  r5.x = 1 / r5.x;
  r1.z = saturate(r5.x * r1.z);
  r5.x = r1.z * -2 + 3;
  r1.z = r1.z * r1.z;
  r1.z = r5.x * r1.z;
  r5.x = 9.99999975e-005 + r3.w;
  r5.x = cmp(r5.x < r0.w);
  if (r5.x != 0) {
    r10.xyz = r6.xyz * r3.www + camera_position.xyz;
    r10.xyz = v1.xyz + -r10.xyz;
    r8.z = dot(r10.xyz, r10.xyz);
    r8.z = sqrt(r8.z);
    r9.y = g_campaign_density_scaling * r8.z;
    r10.w = max(0, v1.y);
    r12.xyzw = float4(-0.000206276105,-0.000206276105,-0.000206276105,-0.00120224594) * r10.wwww;
    r12.xyzw = exp2(r12.xyzw);
    r12.xyzw = r12.xyzw * r9.yyyy;
    r10.xyz = r10.xyz / r8.zzz;
    r8.z = -g_height_fog_clear_distance + r0.w;
    r8.z = max(0, r8.z);
    r13.xyz = float3(0.00999999978,-0.00999999978,0.00999999978) * time_in_sec;
    r13.xyz = v1.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r13.xyz;
    r9.y = t_volume_noise.SampleLevel(s_volume_noise_s, r13.xyz, 0).x;
    r9.y = -0.5 + r9.y;
    r13.y = -0.0799999982 * r9.y;
    r13.xz = float2(0,0);
    r10.xyz = r13.xyz + r10.xyz;
    r9.y = dot(r10.xyz, r10.xyz);
    r9.y = rsqrt(r9.y);
    r9.y = r10.y * r9.y;
    r10.xy = -float2(g_fog_height_top, g_fog_height_bottom) + camera_position.yy;
    r10.z = 10000 + camera_position.y;
    r10.xyz = r10.xyz / -r9.yyy;
    r10.w = min(r10.x, r10.y);
    r10.x = max(r10.x, r10.y);
    r11.z = min(r10.y, r10.z);
    r10.y = max(r10.y, r10.z);
    r10.z = max(0, r11.z);
    r10.xyw = max(float3(0,0,0), r10.xyw);
    r11.z = min(r10.w, r10.x);
    r11.w = max(r10.w, r8.z);
    r10.x = min(r11.w, r10.x);
    r11.w = cmp(r11.z != r10.x);
    r13.x = r11.z * r9.y + camera_position.y;
    r10.x = r10.x + -r10.w;
    r10.w = r11.z + -r10.w;
    r11.z = r10.w + -r10.x;
    r13.x = r13.x + r13.x;
    r10.x = r10.w + r10.x;
    r9.y = r9.y * r10.x + r13.x;
    r9.y = -g_fog_height_top * 2 + r9.y;
    r9.y = r11.z * r9.y;
    r10.x = g_fog_height_top + -g_fog_height_bottom;
    r10.x = r10.x + r10.x;
    r9.y = r9.y / r10.x;
    r9.y = r11.w ? r9.y : 0;
    r10.x = min(r10.z, r10.y);
    r8.z = max(r10.z, r8.z);
    r8.z = min(r8.z, r10.y);
    r10.y = cmp(r10.x != r8.z);
    r8.z = r8.z + -r10.z;
    r10.x = r10.x + -r10.z;
    r8.z = -r10.x + r8.z;
    r8.z = r9.y + r8.z;
    r8.z = r10.y ? r8.z : r9.y;
    r8.z = g_fog_density_height * r8.z;
    r9.y = -g_fog_clear_distance + r0.w;
    r9.y = max(0, r9.y);
    r9.y = g_fog_density_constant * r9.y;
    r8.z = max(r9.y, r8.z);
    r10.xyzw = float4(5.80000005e-006,1.35e-005,3.3100001e-005,2.20000002e-005) * r12.xyzw;
    r10.xyz = r10.xyz + r10.www;
    r10.xyz = r8.zzz * float3(0.0125000002,0.0125000002,0.0125000002) + r10.xyz;
    r10.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r10.xyz;
    r10.xyz = exp2(r10.xyz);
  } else {
    r10.xyz = float3(1,1,1);
  }
  r10.xyz = r1.zzz * -r10.xyz + r10.xyz;
  r10.xyz = float3(-1,-1,-1) + r10.xyz;
  r10.xyz = g_fog_lerp * r10.xyz + float3(1,1,1);
  r12.xyz = r1.xyw / r0.www;
  r8.z = dot(-r12.xyz, sun_direction.xyz);
  if (r5.x != 0) {
    r13.xyz = r6.xyz * r3.www + camera_position.xyz;
    r14.xyz = v1.xyz + -r13.xyz;
    r3.w = dot(r14.xyz, r14.xyz);
    r3.w = sqrt(r3.w);
    r11.zw = r13.xz * float2(0.00100000005,0.00100000005) + v0.xy;
    r11.zw = r12.xy * float2(0.100000001,0.100000001) + r11.zw;
    r11.zw = g_camera_jitter.xy + r11.zw;
    r11.zw = float2(0.103100002,0.103100002) * r11.zw;
    r11.zw = frac(r11.zw);
    r14.xyz = float3(19.1900005,19.1900005,19.1900005) + r11.wzz;
    r5.x = dot(r11.zwz, r14.xyz);
    r11.zw = r11.zw + r5.xx;
    r5.x = r11.z + r11.w;
    r5.x = r5.x * r11.z;
    r5.x = frac(r5.x);
    r5.x = frac(r5.x);
    r9.y = -5000 + r3.w;
    r9.y = saturate(0.000125000006 * r9.y);
    r10.w = r9.y * -2 + 3;
    r9.y = r9.y * r9.y;
    r9.y = r10.w * r9.y;
    r10.w = 0.25 + -r5.x;
    r5.x = r9.y * r10.w + r5.x;
    r11.zw = float2(0.0625,0.03125) * r3.ww;
    r9.y = cmp(0 < g_fog_noise);
    r14.xy = float2(0.00999999978,-0.00999999978) * time_in_sec;
    r15.xyz = float3(0,0,0);
    r16.xyz = float3(0,0,0);
    r14.zw = float2(0,0);
    r10.w = 0;
    r12.w = 0;
    while (true) {
      r13.w = cmp((int)r12.w >= 16);
      if (r13.w != 0) break;
      r13.w = (int)r12.w;
      r13.w = r13.w + r5.x;
      r13.w = r11.z * r13.w + r11.w;
      r13.w = min(r13.w, r3.w);
      r17.xyz = r12.xyz * r13.www + r13.xyz;
      r13.w = cmp(r13.w < 400);
      r13.w = r9.y ? r13.w : 0;
      if (r13.w != 0) {
        r17.xzw = time_in_sec * float3(4.4000001,-4.4000001,4.4000001) + r17.xyz;
        r17.xzw = float3(0.00999999978,0.00999999978,0.00999999978) * r17.xzw;
        r13.w = t_volume_noise.SampleLevel(s_volume_noise_s, r17.xzw, 0).x;
        r13.w = r13.w * 2 + -1;
        r13.w = r13.w * g_fog_noise + 1;
      } else {
        r13.w = 1;
      }
      r10.w = r13.w + r10.w;
      r13.w = 60000 + -r17.y;
      r15.w = r13.w * 0.5 + r17.y;
      r15.w = max(0, r15.w);
      r17.xz = float2(-0.000206276105,-0.00120224594) * r15.ww;
      r17.xz = exp2(r17.xz);
      r15.w = g_fog_density_mie * r17.z;
      r16.w = max(0, r17.y);
      r17.yz = float2(-0.000206276105,-0.00120224594) * r16.ww;
      r17.yz = exp2(r17.yz);
      r16.w = r17.y * r11.z;
      r14.z = r17.y * r11.z + r14.z;
      r17.y = g_fog_density_mie * r17.z;
      r17.z = r17.y * r11.z;
      r14.w = r17.y * r11.z + r14.w;
      r17.x = r17.x * r13.w + r14.z;
      r13.w = r15.w * r13.w + r14.w;
      r13.w = 2.20000002e-005 * r13.w;
      r17.xyw = r17.xxx * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r13.www;
      r17.xyw = float3(-1.44269502,-1.44269502,-1.44269502) * r17.xyw;
      r17.xyw = exp2(r17.xyw);
      r15.xyz = r17.xyw * r16.www + r15.xyz;
      r16.xyz = r17.xyw * r17.zzz + r16.xyz;
      r12.w = (int)r12.w + 1;
    }
    r3.w = r8.z * r8.z + 1;
    r5.x = 0.5 * r3.w;
    r13.xyz = r15.xyz * r5.xxx;
    r5.x = -r8.z * 1.51999998 + 1.5776;
    r5.x = log2(r5.x);
    r5.x = 1.5 * r5.x;
    r5.x = exp2(r5.x);
    r3.w = r3.w / r5.x;
    r3.w = 0.000978047145 * r3.w;
    r15.xyz = r16.xyz * r3.www;
    r15.xyz = float3(1.99999995e-005,1.99999995e-005,1.99999995e-005) * r15.xyz;
    r13.xyz = r13.xyz * float3(5.80000005e-006,1.35e-005,3.3100001e-005) + r15.xyz;
    r3.w = 0.0625 * r10.w;
    r5.x = -g_height_fog_clear_distance + r0.w;
    r5.x = max(0, r5.x);
    r14.xyz = v1.xyz * float3(0.00100000005,0.00100000005,0.00100000005) + r14.xyx;
    r9.y = t_volume_noise.SampleLevel(s_volume_noise_s, r14.xyz, 0).x;
    r9.y = -0.5 + r9.y;
    r14.y = -0.0799999982 * r9.y;
    r14.xz = float2(0,0);
    r12.xyz = r14.xyz + r12.xyz;
    r9.y = dot(r12.xyz, r12.xyz);
    r9.y = rsqrt(r9.y);
    r9.y = r12.y * r9.y;
    r10.w = r9.y * g_height_fog_clear_distance + camera_position.y;
    r11.z = cmp(r10.w >= g_fog_height_top);
    r11.w = cmp(0 < r9.y);
    r11.z = r11.w ? r11.z : 0;
    if (r11.z == 0) {
      r11.zw = -float2(g_fog_height_top, g_fog_height_bottom) + r10.ww;
      r11.zw = r11.zw / -r9.yy;
      r12.x = 10000 + r10.w;
      r12.x = r12.x / -r9.y;
      r12.y = min(r11.z, r11.w);
      r11.z = max(r11.z, r11.w);
      r12.z = min(r12.x, r11.w);
      r11.w = max(r12.x, r11.w);
      r12.xy = max(float2(0,0), r12.zy);
      r11.zw = max(float2(0,0), r11.zw);
      r12.z = g_fog_density_height * r3.w;
      r12.z = 0.0125000002 * r12.z;
      r12.w = min(r12.y, r11.z);
      r13.w = max(r12.y, r5.x);
      r13.w = min(r13.w, r11.z);
      r14.x = cmp(r12.w != r13.w);
      r10.w = r12.y * r9.y + r10.w;
      r13.w = r13.w + -r12.y;
      r14.y = r12.w + -r12.y;
      r14.z = g_fog_height_top + -g_fog_height_bottom;
      r14.z = r14.z + r14.z;
      r10.w = r10.w + r10.w;
      r14.w = r14.y * r9.y + r10.w;
      r14.w = -g_fog_height_top * 2 + r14.w;
      r14.y = r14.y * r14.w;
      r14.y = r14.y * r12.z;
      r14.y = r14.y / r14.z;
      r14.y = 1.44269502 * r14.y;
      r14.y = exp2(r14.y);
      r14.w = r13.w * r9.y + r10.w;
      r14.w = -g_fog_height_top * 2 + r14.w;
      r13.w = r14.w * r13.w;
      r13.w = r13.w * r12.z;
      r13.w = r13.w / r14.z;
      r13.w = 1.44269502 * r13.w;
      r13.w = exp2(r13.w);
      r13.w = r14.y + -r13.w;
      r14.y = min(r12.x, r11.w);
      r14.w = max(r12.w, r12.x);
      r14.w = min(r14.w, r11.w);
      r15.x = cmp(r14.y != r14.w);
      r14.w = r14.w + -r12.x;
      r14.w = -r14.w * r12.z;
      r14.w = 1.44269502 * r14.w;
      r14.w = exp2(r14.w);
      r14.w = r14.w * r13.w;
      r13.w = r15.x ? r14.w : r13.w;
      r13.w = r14.x ? r13.w : 0;
      r14.x = max(r12.x, r5.x);
      r11.w = min(r14.x, r11.w);
      r14.x = cmp(r14.y != r11.w);
      r14.w = r11.w + -r12.x;
      r12.x = r14.y + -r12.x;
      r12.x = -r12.x * r12.z;
      r12.x = 1.44269502 * r12.x;
      r12.x = exp2(r12.x);
      r14.y = -r14.w * r12.z;
      r14.y = 1.44269502 * r14.y;
      r14.y = exp2(r14.y);
      r12.x = -r14.y + r12.x;
      r11.w = max(r11.w, r12.y);
      r11.z = min(r11.w, r11.z);
      r11.w = cmp(r12.w != r11.z);
      r11.z = r11.z + -r12.y;
      r9.y = r11.z * r9.y + r10.w;
      r9.y = -g_fog_height_top * 2 + r9.y;
      r9.y = r11.z * r9.y;
      r9.y = r9.y * r12.z;
      r9.y = r9.y / r14.z;
      r9.y = 1.44269502 * r9.y;
      r9.y = exp2(r9.y);
      r9.y = r12.x * r9.y;
      r9.y = r11.w ? r9.y : r12.x;
      r9.y = r13.w + r9.y;
      r12.xyz = r14.xxx ? r9.yyy : r13.www;
    } else {
      r12.xyz = float3(0,0,0);
    }
    r9.y = 1 / g_clear_distance_fade_out;
    r5.x = saturate(r9.y * r5.x);
    r9.y = r5.x * -2 + 3;
    r5.x = r5.x * r5.x;
    r5.x = r9.y * r5.x;
    r12.xyz = r12.xyz * r5.xxx;
    r5.x = -g_fog_clear_distance + r0.w;
    r5.x = max(0, r5.x);
    r9.y = cmp(0.00999999978 < g_clear_distance_fade_out);
    r10.w = cmp(0.00999999978 < r5.x);
    r9.y = r9.y ? r10.w : 0;
    r10.w = min(g_clear_distance_fade_out, r5.x);
    r11.z = r10.w * r10.w;
    r11.w = g_clear_distance_fade_out + g_clear_distance_fade_out;
    r11.z = r11.z / r11.w;
    r10.w = -r10.w + r5.x;
    r10.w = r11.z + r10.w;
    r10.w = r10.w / r5.x;
    r9.y = r9.y ? r10.w : 1;
    r3.w = g_fog_density_constant * r3.w;
    r3.w = r3.w * r9.y;
    r3.w = r3.w * r5.x;
    r3.w = 0.0180336889 * r3.w;
    r5.x = exp2(r3.w);
    r3.w = exp2(-r3.w);
    r9.y = r5.x + r3.w;
    r3.w = r5.x + -r3.w;
    r3.w = 0.5 * r3.w;
    r3.w = saturate(r9.y * 0.5 + -r3.w);
    r3.w = 1 + -r3.w;
    r12.xyz = max(r12.xyz, r3.www);
  } else {
    r12.xyz = float3(0,0,0);
    r13.xyz = float3(0,0,0);
  }
  r12.xyz = max(r12.xyz, r1.zzz);
  r1.z = r8.z * r8.z + 1;
  r1.z = 0.5 * r1.z;
  r13.xyz = r12.xyz * r1.zzz + r13.xyz;
  r14.xyz = r12.xyz * r1.zzz + -r13.xyz;
  r12.xyz = r12.xyz * r14.xyz + r13.xyz;
  r12.xyz = g_fog_colour.xyz * r12.xyz;
  if (g_use_spherical_harmonics != 0) {
    r0.w = 1 / r0.w;
    r0.w = min(1, r0.w);
    r11.zw = r0.ww * r1.xw + camera_position.xz;
    r11.zw = -g_probe_grid_area.xy + r11.zw;
    r11.xy = r11.zw / r11.xy;
    r5.xy = r11.xy * r5.yy + float2(-0.5,-0.5);
    r5.xy = max(float2(0,0), r5.xy);
    r5.xy = min(r5.xy, r6.ww);
    r11.xy = floor(r5.xy);
    r5.xy = frac(r5.xy);
    r11.xy = (int2)r11.xy;
    r11.zw = (int2)r11.xy * int2(3,3);
    r0.w = mad(g_probe_grid_size, 3, -3);
    r11.xy = mad((int2)r11.xy, int2(3,3), int2(3,3));
    r11.xyzw = min((uint4)r11.xyzw, (uint4)r0.wwww);
    r0.w = round(g_spherical_harmonic_terms);
    r0.w = (int)r0.w;
    r13.w = 0;
    r14.xyz = float3(0,0,0);
    r1.z = 0;
    while (true) {
      r3.w = cmp((int)r1.z >= (int)r0.w);
      if (r3.w != 0) break;
      r3.w = (uint)r1.z % 3;
      r13.xy = (int2)r3.ww + (int2)r11.xz;
      r13.z = (int)r11.w + (int)icb[r1.z+0].y;
      r15.xyz = t_world_spherical_harmonics.Load(r13.yzw).xyz;
      r16.xyz = t_world_spherical_harmonics.Load(r13.xzw).xyz;
      r17.y = (int)r11.y + (int)icb[r1.z+0].y;
      r17.xzw = r13.yww;
      r18.xyz = t_world_spherical_harmonics.Load(r17.xyz).xyz;
      r17.xzw = r13.xww;
      r13.xyz = t_world_spherical_harmonics.Load(r17.xyz).xyz;
      r16.xyz = r16.xyz + -r15.xyz;
      r15.xyz = r5.xxx * r16.xyz + r15.xyz;
      r13.xyz = r13.xyz + -r18.xyz;
      r13.xyz = r5.xxx * r13.xyz + r18.xyz;
      r13.xyz = r13.xyz + -r15.xyz;
      r13.xyz = r5.yyy * r13.xyz + r15.xyz;
      r14.xyz = r13.xyz * icb[r1.z+0].zzz + r14.xyz;
      r1.z = (int)r1.z + 1;
    }
    r13.xyz = max(float3(0,0,0), r14.xyz);
    if (r8.w != 0) {
      r14.w = 0;
      r15.xyz = float3(0,0,0);
      r1.z = 0;
      while (true) {
        r3.w = cmp((int)r1.z >= (int)r0.w);
        if (r3.w != 0) break;
        r3.w = (uint)r1.z % 3;
        r14.xy = (int2)r3.ww + (int2)r11.xz;
        r14.z = (int)r11.w + (int)icb[r1.z+0].y;
        r16.xyz = t_prev_world_spherical_harmonics.Load(r14.yzw).xyz;
        r17.xyz = t_prev_world_spherical_harmonics.Load(r14.xzw).xyz;
        r18.y = (int)r11.y + (int)icb[r1.z+0].y;
        r18.xzw = r14.yww;
        r19.xyz = t_prev_world_spherical_harmonics.Load(r18.xyz).xyz;
        r18.xzw = r14.xww;
        r14.xyz = t_prev_world_spherical_harmonics.Load(r18.xyz).xyz;
        r17.xyz = r17.xyz + -r16.xyz;
        r16.xyz = r5.xxx * r17.xyz + r16.xyz;
        r14.xyz = r14.xyz + -r19.xyz;
        r14.xyz = r5.xxx * r14.xyz + r19.xyz;
        r14.xyz = r14.xyz + -r16.xyz;
        r14.xyz = r5.yyy * r14.xyz + r16.xyz;
        r15.xyz = r14.xyz * icb[r1.z+0].zzz + r15.xyz;
        r1.z = (int)r1.z + 1;
      }
      r11.xyz = max(float3(0,0,0), r15.xyz);
      r14.xyz = r13.xyz + -r11.xyz;
      r11.xyz = g_specular_probe_lerp * r14.xyz + r11.xyz;
    }
    r11.xyz = r8.www ? r11.xyz : r13.xyz;
    r11.xyz = g_ambient_fudge_factor * r11.xyz;
  } else {
    r11.xyz = g_ambient_fudge_factor * ambient_cube_tb[0].xyz;
  }
  r11.xyz = sun_colour.xyz * float3(1.90384671e-005,1.90384671e-005,1.90384671e-005) + r11.xyz;
  r11.xyz = r12.xyz * r11.xyz;
  r11.xyz = g_fog_lerp * r11.xyz;
  r7.xyz = saturate(r10.xyz * r7.xyz + r11.xyz);
  r7.xyz = log2(r7.xyz);
  r7.xyz = g_inv_gamma_output * r7.xyz;
  r7.xyz = exp2(r7.xyz);
  r0.w = cmp(0.200000003 < g_point_light_intensity);
  if (r0.w != 0) {
    r10.xyz = g_point_light_intensity * g_point_light_color.xyz;
    r11.xyz = -g_point_light_position.xyz + v1.xyz;
    r0.w = dot(r11.xyz, r11.xyz);
    r1.z = rsqrt(r0.w);
    r12.xyz = r11.xyz * r1.zzz;
    r0.w = sqrt(r0.w);
    r0.w = saturate(r0.w / g_point_light_radius);
    r0.w = 1 + -r0.w;
    r10.xyz = r0.www * r10.xyz;
    r0.w = dot(r3.xyz, -r12.xyz);
    r3.w = saturate(r0.w);
    r6.xyz = -r11.xyz * r1.zzz + -r6.xyz;
    r1.z = dot(r6.xyz, r6.xyz);
    r1.z = rsqrt(r1.z);
    r6.xyz = r6.xyz * r1.zzz;
    r1.z = dot(r3.xyz, r6.xyz);
    r3.x = r5.w * r5.w;
    r1.z = r1.z * r1.z;
    r3.y = r5.w * r5.w + -1;
    r1.z = r1.z * r3.y + 1;
    r1.z = r1.z * r1.z;
    r1.z = 3.14159274 * r1.z;
    r1.z = r3.x / r1.z;
    r3.x = -1 + r3.w;
    r3.x = r8.x * r3.x + 1;
    r3.y = r3.w * r9.w + r9.z;
    r3.y = 1 / r3.y;
    r3.z = r9.x * r9.w + r9.z;
    r3.z = 1 / r3.z;
    r3.y = r3.y * r3.z;
    r3.y = 0.25 * r3.y;
    r3.y = min(1, r3.y);
    r3.x = r3.y * r3.x;
    r1.xyw = r1.xyw * r2.www + -r12.xyz;
    r2.w = dot(r1.xyw, r1.xyw);
    r2.w = rsqrt(r2.w);
    r1.xyw = r2.www * r1.xyw;
    r1.x = saturate(dot(r1.xyw, -r12.xyz));
    r1.y = r1.x * -5.55472994 + -6.98316002;
    r1.x = r1.y * r1.x;
    r1.x = exp2(r1.x);
    r1.xyw = r4.xyz * r1.xxx + r2.xyz;
    r1.z = r3.x * r1.z;
    r1.xyz = r1.zzz * r1.xyw;
    r1.w = r3.w * -5.55472994 + -6.98316002;
    r1.w = r1.w * r3.w;
    r1.w = exp2(r1.w);
    r2.xyz = r4.xyz * r1.www + r2.xyz;
    r2.xyz = float3(1,1,1) + -r2.xyz;
    r0.w = max(-1, r0.w);
    r0.w = min(1, r0.w);
    r1.w = max(-1, r4.w);
    r1.w = min(1, r1.w);
    r2.w = 1 + -abs(r0.w);
    r2.w = sqrt(r2.w);
    r3.x = abs(r0.w) * -0.0187292993 + 0.0742610022;
    r3.x = r3.x * abs(r0.w) + -0.212114394;
    r3.x = r3.x * abs(r0.w) + 1.57072878;
    r3.y = r3.x * r2.w;
    r3.y = r3.y * -2 + 3.14159274;
    r3.z = cmp(r0.w < -r0.w);
    r3.y = r3.z ? r3.y : 0;
    r2.w = r3.x * r2.w + r3.y;
    r3.x = 1 + -abs(r1.w);
    r3.x = sqrt(r3.x);
    r3.y = abs(r1.w) * -0.0187292993 + 0.0742610022;
    r3.y = r3.y * abs(r1.w) + -0.212114394;
    r3.y = r3.y * abs(r1.w) + 1.57072878;
    r3.z = r3.y * r3.x;
    r3.z = r3.z * -2 + 3.14159274;
    r1.w = cmp(r1.w < -r1.w);
    r1.w = r1.w ? r3.z : 0;
    r1.w = r3.y * r3.x + r1.w;
    r3.x = max(r1.w, r2.w);
    r3.y = min(r1.w, r2.w);
    r1.w = r2.w + -r1.w;
    r1.w = cos(r1.w);
    r1.w = max(0, r1.w);
    r1.w = r7.w * r1.w;
    r2.w = sin(r3.x);
    r1.w = r2.w * r1.w;
    sincos(r3.y, r3.x, r4.x);
    r2.w = r3.x / r4.x;
    r1.w = r1.w * r2.w + r5.z;
    r0.w = max(0, r0.w);
    r0.w = r1.w * r0.w;
    r2.xyz = r10.xyz * r2.xyz;
    r0.xyz = r2.xyz * r0.xyz;
    r0.w = r8.y ? r3.w : r0.w;
    r0.xyz = r0.xyz * r0.www;
    r0.xyz = r1.xyz * r10.xyz + r0.xyz;
    o0.xyz = r7.xyz + r0.xyz;
    o0.w = 2;
  } else {
    o0.xyz = r7.xyz;
    o0.w = 1;
  }
  o0 = saturate(o0);  // RenoDX
  o1.xyzw = float4(1,0,0,1);
  return;
}
