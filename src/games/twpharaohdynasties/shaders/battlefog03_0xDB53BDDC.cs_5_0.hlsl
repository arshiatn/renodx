// battlefog.hlsl cs50_sun_scan (sm_5_0): the game's (3Dmigoto's decompile, checked against its disassembly: the thread
// group and shared memory declared, a structured load's index kept apart from its result, the float infinity test on its
// bits, -pi to the bit), with the screen-edge fade from the view's uv. 32 x 32 rays around the sun; u0 = the visible
// rays' texture uv, their visibility and the fade. The game takes the fade from the texture uv (the view's uv x the
// viewport's share of its target, c29.xy x c31.zw), so below 100% (DLSS, FSR, XeSS) the fog's sun light faded wherever
// the sun stood in the view's left or top part. The rest is the game's: the sun's uv stays the texture's (its readers'),
// its visibility reads the depth there.
groupshared struct { float val[4]; } g0[1024];

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
  float g_overlay_text_lerp : packoffset(c47.y);
  float g_overlay_parchment_lerp : packoffset(c47.z);
  float g_overlay_palette_alpha : packoffset(c47.w);
  float4 g_overlay_outline_colour : packoffset(c48);
  float g_overlay_outline_border_width : packoffset(c49);
  float g_ui_overlay_brightness : packoffset(c49.y);
  float g_debug_tonemapping : packoffset(c49.z);
  float4 g_blood_remap : packoffset(c50);
  int g_editor_mode : packoffset(c51);
  float4 g_spec_gloss_tweaker : packoffset(c52);
}

cbuffer environment_PS : register(b1)
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

cbuffer lighting_VS_PS : register(b2)
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

cbuffer fog : register(b3)
{
  float g_fog_density_constant : packoffset(c0);
  float g_fog_density_height : packoffset(c0.y);
  float g_fog_height_bottom : packoffset(c0.z);
  float g_fog_height_top : packoffset(c0.w);
  float3 g_fog_noise_position : packoffset(c1);
  float g_fog_density_mie : packoffset(c1.w);
  float g_fog_noise : packoffset(c2);
  float3 g_fog_colour : packoffset(c2.y);
  uint g_fog_mode : packoffset(c3);
  float g_fog_lerp : packoffset(c3.y);
  float g_fog_clear_distance : packoffset(c3.z);
  float g_fog_y_multiplier : packoffset(c3.w);
  float g_campaign_density_scaling : packoffset(c4);
  float g_height_fog_clear_distance : packoffset(c4.y);
  float g_clear_distance_fade_out : packoffset(c4.z);
  float g_fog_far_skybox_lerp : packoffset(c4.w);
  float g_fog_far_skybox_mip : packoffset(c5);
  float2 g_fog_shadow_strength : packoffset(c5.y);
  float g_fog_blur_offset : packoffset(c5.w);
  float g_fog_blur_strength : packoffset(c6);
  float3 g_height_fog_colour : packoffset(c6.y);
  float g_height_fog_falloff : packoffset(c7);
  float g_height_fog_clear_falloff : packoffset(c7.y);
  float g_height_fog_forward_scattering : packoffset(c7.z);
  float2 g_campaign_map_size : packoffset(c8);
  float3 g_rayleigh_density : packoffset(c9);
  float g_atmosphere_height_multiplier : packoffset(c9.w);
  float g_atmosphere_bottom : packoffset(c10);
  float g_sun_disc_color_scale : packoffset(c10.y);
  float g_sun_scale : packoffset(c10.z);
  float g_fog_resolution : packoffset(c10.w);
  float g_height_occlusion : packoffset(c11);
  float g_density_sum : packoffset(c11.y);
  float g_skybox_size : packoffset(c11.z);
}

SamplerState s_skybox_s : register(s0);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float4> t_skybox : register(t2);
Buffer<float4> b_SunPositionBuffer : register(t3);
RWBuffer<float4> b_RWSunPositionBuffer : register(u0);


// 3Dmigoto declarations
#define cmp -


[numthreads(32, 32, 1)]
void main(uint3 vThreadIDInGroup : SV_GroupThreadID, uint vThreadIDInGroupFlattened : SV_GroupIndex)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 1000 * g_sun_scale;
  r0.yzw = float3(0,-1,0) * sun_disk_direction.zxy;
  r1.xyz = -sun_disk_direction.zxy * float3(-1,0,0) + r0.wyz;
  r1.w = dot(r1.xz, r1.xz);
  r1.w = cmp(r1.w < 1.00000001e-010);
  r0.yzw = sun_disk_direction.yzx * float3(-1,0,0) + -r0.yzw;
  r0.yzw = r1.www ? r0.yzw : r1.xyz;
  r1.x = dot(r0.yzw, r0.yzw);
  r1.x = rsqrt(r1.x);
  r0.yzw = r1.xxx * r0.yzw;
  r1.xyz = -sun_disk_direction.yzx * r0.wyz;
  r1.xyz = r0.zwy * -sun_disk_direction.zxy + -r1.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.xyz * r1.www;
  r2.xy = (uint2)vThreadIDInGroup.xy;
  r2.xy = float2(0.03125,0.196349546) * r2.xy;
  r1.w = sqrt(r2.x);
  r0.x = r1.w * r0.x;
  sincos(r2.y, r2.x, r3.x);
  r1.w = r3.x * r0.x;
  r0.x = r2.x * r0.x;
  r1.xyz = r1.www * r1.xyz;
  r1.xyz = sun_disk_direction.xyz * float3(-10000,-10000,-10000) + r1.xyz;
  r0.xyz = r0.xxx * r0.yzw + r1.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.xyz * r0.www;
  r2.x = dot(r1.xyz, view_projection._m00_m10_m20);
  r2.y = dot(r1.xyz, view_projection._m01_m11_m21);
  r0.x = dot(r1.xyz, view_projection._m02_m12_m22);
  r0.z = dot(r1.xyz, view_projection._m03_m13_m23);
  r1.yw = r2.xy / r0.zz;
  r2.xy = g_render_target_dimensions.zw * g_viewport_dimensions.xy;
  r2.zw = r1.yw * float2(0.5,-0.5) + float2(0.5,0.5);
  const float2 view_uv = r2.zw;
  r3.xy = r2.zw * r2.xy;
  r0.x = cmp(0 < r0.x);
  r1.yw = cmp(float2(1,1) >= abs(r1.yw));
  r0.z = r1.w ? r1.y : 0;
  r0.x = r0.x ? r0.z : 0;
  r0.z = cmp(1 < g_num_of_samples);
  if (r0.z != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r1.yw = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r1.yw = uiDest.xy;
  }
  r4.xy = (uint2)r1.yw;
  r4.xy = r3.xy * r4.xy + float2(0.5,0.5);
  r4.xy = floor(r4.xy);
  r4.xy = (int2)r4.xy;
  r1.yw = (int2)r1.yw + int2(-1,-1);
  r4.xy = max(int2(0,0), (int2)r4.xy);
  r4.xy = min((int2)r4.xy, (int2)r1.yw);
  if (r0.z != 0) {
    r4.z = 0;
    r0.z = gbuffer_channel_4_texture_ms.Load(r4.xy, 0).x;
  } else {
    r4.w = 0;
    r0.z = gbuffer_channel_4_texture.Load(r4.xyw).x;
  }
  r0.z = r0.x ? r0.z : 0;
  r0.z = cmp(0.999989986 < r0.z);
  r0.z = r0.z ? 1.000000 : 0;
  r1.y = min(abs(r1.x), abs(r1.z));
  r1.w = max(abs(r1.x), abs(r1.z));
  r1.w = 1 / r1.w;
  r1.y = r1.y * r1.w;
  r1.w = r1.y * r1.y;
  r4.x = r1.w * 0.0208350997 + -0.0851330012;
  r4.x = r1.w * r4.x + 0.180141002;
  r4.x = r1.w * r4.x + -0.330299497;
  r1.w = r1.w * r4.x + 0.999866009;
  r4.x = r1.y * r1.w;
  r4.y = cmp(abs(r1.z) < abs(r1.x));
  r4.x = r4.x * -2 + 1.57079637;
  r4.x = r4.y ? r4.x : 0;
  r1.y = r1.y * r1.w + r4.x;
  r1.w = cmp(r1.z < -r1.z);
  r1.w = r1.w ? -3.14159274 : 0;  // the game's l(0xc0490fdb): -pi to the bit
  r1.y = r1.y + r1.w;
  r1.w = min(r1.x, r1.z);
  r1.x = max(r1.x, r1.z);
  r1.z = cmp(r1.w < -r1.w);
  r1.x = cmp(r1.x >= -r1.x);
  r1.x = r1.x ? r1.z : 0;
  r1.x = r1.x ? -r1.y : r1.y;
  r0.y = r0.y * r0.w + 0.0250000004;
  r0.y = 0.975609779 * r0.y;
  r0.w = 1 + -abs(r0.y);
  r0.w = sqrt(r0.w);
  r1.y = abs(r0.y) * -0.0187292993 + 0.0742610022;
  r1.y = r1.y * abs(r0.y) + -0.212114394;
  r1.y = r1.y * abs(r0.y) + 1.57072878;
  r1.z = r1.y * r0.w;
  r1.z = r1.z * -2 + 3.14159274;
  r0.y = cmp(r0.y < -r0.y);
  r0.y = r0.y ? r1.z : 0;
  r0.y = r1.y * r0.w + r0.y;
  r0.y = 0.636619747 * r0.y;
  r4.y = min(1, r0.y);
  r4.x = r1.x * 0.159154937 + -g_skydome_params.x;
  r0.y = t_skybox.SampleLevel(s_skybox_s, r4.xy, 0).w;
  r1.z = r0.z * r0.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4] = r0.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4+1] = r0.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4+2] = r0.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4+3] = r0.y;
  GroupMemoryBarrierWithGroupSync();
  r0.y = (uint)vThreadIDInGroupFlattened.x << 1;
  r0.z = 1;
  r0.w = r0.y;
  while (true) {
    r4.x = cmp((uint)r0.z >= 1024);
    if (r4.x != 0) break;
    r4.x = g0[r0.w].val[0/4];
    r4.y = g0[r0.w].val[0/4+1];
    r4.z = g0[r0.w].val[0/4+2];
    r4.w = g0[r0.w].val[0/4+3];
    const int partner = (int)r0.z + (int)r0.w;  // ld_structured r5.xyzw, r5.x: the index kept apart
    r5.x = g0[partner].val[0/4];
    r5.y = g0[partner].val[0/4+1];
    r5.z = g0[partner].val[0/4+2];
    r5.w = g0[partner].val[0/4+3];
    r4.xyzw = r5.xyzw + r4.xyzw;
    g0[r0.w].val[0/4] = r4.x;
    g0[r0.w].val[0/4+1] = r4.y;
    g0[r0.w].val[0/4+2] = r4.z;
    g0[r0.w].val[0/4+3] = r4.w;
    GroupMemoryBarrierWithGroupSync();
    r0.zw = (uint2)r0.zw << int2(1,1);
  }
  r0.z = g0[0].val[0/4];
  r1.xy = r3.xy * r1.zz;
  r1.w = r0.x ? 1.000000 : 0;
  g0[vThreadIDInGroupFlattened.x].val[0/4] = r1.x;
  g0[vThreadIDInGroupFlattened.x].val[0/4+1] = r1.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4+2] = r1.z;
  g0[vThreadIDInGroupFlattened.x].val[0/4+3] = r1.w;
  GroupMemoryBarrierWithGroupSync();
  r0.x = 1;
  r0.w = r0.y;
  while (true) {
    r1.x = cmp((uint)r0.x >= 1024);
    if (r1.x != 0) break;
    r4.x = g0[r0.w].val[0/4];
    r4.y = g0[r0.w].val[0/4+1];
    r4.z = g0[r0.w].val[0/4+2];
    r4.w = g0[r0.w].val[0/4+3];
    r1.x = (int)r0.x + (int)r0.w;
    r5.x = g0[r1.x].val[0/4];
    r5.y = g0[r1.x].val[0/4+1];
    r5.z = g0[r1.x].val[0/4+2];
    r5.w = g0[r1.x].val[0/4+3];
    r4.xyzw = r5.xyzw + r4.xyzw;
    g0[r0.w].val[0/4] = r4.x;
    g0[r0.w].val[0/4+1] = r4.y;
    g0[r0.w].val[0/4+2] = r4.z;
    g0[r0.w].val[0/4+3] = r4.w;
    GroupMemoryBarrierWithGroupSync();
    r0.xw = (uint2)r0.xw << int2(1,1);
  }
  r4.x = g0[0].val[0/4+3];
  r4.y = g0[0].val[0/4];
  r4.z = g0[0].val[0/4+1];
  r4.w = g0[0].val[0/4+2];
  r0.xw = r4.yz / r4.ww;
  r0.xw = r2.zw * r2.xy + -r0.xw;
  GroupMemoryBarrierWithGroupSync();
  r3.w = dot(r0.xw, r0.xw);
  r3.z = r1.z;
  g0[vThreadIDInGroupFlattened.x].val[0/4] = r3.x;
  g0[vThreadIDInGroupFlattened.x].val[0/4+1] = r3.y;
  g0[vThreadIDInGroupFlattened.x].val[0/4+2] = r3.z;
  g0[vThreadIDInGroupFlattened.x].val[0/4+3] = r3.w;
  GroupMemoryBarrierWithGroupSync();
  r0.x = 1;
  r0.w = r0.y;
  while (true) {
    r1.x = cmp((uint)r0.x >= 1024);
    if (r1.x != 0) break;
    r1.x = g0[r0.w].val[0/4];
    r1.y = g0[r0.w].val[0/4+1];
    r1.z = g0[r0.w].val[0/4+2];
    r1.w = g0[r0.w].val[0/4+3];
    r2.x = (int)r0.x + (int)r0.w;
    r5.x = g0[r2.x].val[0/4];
    r5.y = g0[r2.x].val[0/4+1];
    r5.z = g0[r2.x].val[0/4+2];
    r5.w = g0[r2.x].val[0/4+3];
    r2.y = cmp(r1.w < r5.w);
    r2.z = cmp(0 < r1.z);
    r2.y = r2.z ? r2.y : 0;
    r2.z = cmp(r5.z == 0.000000);
    r2.y = (int)r2.z | (int)r2.y;
    r2.x = cmp(1024 < (uint)r2.x);
    r2.x = (int)r2.x | (int)r2.y;
    r1.xyzw = r2.xxxx ? r1.xyzw : r5.xyzw;
    g0[r0.w].val[0/4] = r1.x;
    g0[r0.w].val[0/4+1] = r1.y;
    g0[r0.w].val[0/4+2] = r1.z;
    g0[r0.w].val[0/4+3] = r1.w;
    GroupMemoryBarrierWithGroupSync();
    r0.xw = (uint2)r0.xw << int2(1,1);
  }
  if (vThreadIDInGroupFlattened.x == 0) {
    r0.x = g_height_occlusion * r0.z;
    r0.x = 0.0009765625 * r0.x;
    r0.y = 0.0009765625 * r4.x;
    r0.z = r4.w * 0.0009765625 + -r0.x;
    r0.x = r0.y * r0.z + r0.x;
    r0.y = b_SunPositionBuffer.Load(0).z;
    r0.z = cmp((asuint(r0.x) & 0x7f800000u) == 0x7f800000u);  // its bits (the decompile truncated the float)
    r0.x = r0.z ? 0 : r0.x;
    r0.x = r0.x + -r0.y;
    r0.z = r0.x * 0.100000001 + r0.y;
    r0.x = g0[0].val[0/4];
    r0.y = g0[0].val[0/4+1];
    r1.x = dot(-sun_disk_direction.xyz, view._m00_m10_m20);
    r1.y = dot(-sun_disk_direction.xyz, view._m01_m11_m21);
    r1.z = dot(-sun_disk_direction.xyz, view._m02_m12_m22);
    r1.w = dot(-sun_disk_direction.xyz, view._m03_m13_m23);
    r1.x = dot(r1.xyzw, projection._m02_m12_m22_m32);
    r1.x = cmp(0 < r1.x);
    r1.x = r1.x ? 1.000000 : 0;
    r1.yz = view_uv * float2(2,2) + float2(-1,-1);
    r1.y = max(abs(r1.y), abs(r1.z));
    r1.y = -0.699999988 + r1.y;
    r1.y = saturate(3.33333325 * r1.y);
    r1.z = r1.y * -2 + 3;
    r1.y = r1.y * r1.y;
    r1.y = -r1.z * r1.y + 1;
    r4.x = saturate(r4.x);
    r1.x = r4.x * r1.x;
    r0.w = r1.x * r1.y;
    b_RWSunPositionBuffer[0] = r0.xyzw;
  }
  return;
}