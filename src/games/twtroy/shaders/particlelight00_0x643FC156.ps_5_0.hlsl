// particle_vfx_main ps_main_deferred_dynamic_light (the particles' lights: the night siege's fires), the game's 643FC156
// (3Dmigoto decompile) in the point lights' form (pointlight00, Pharaoh's fire02): x the light's traced visibility (Torch
// and Fire Shadows: twattila/rt_scene.hpp InstallPointLights with LightInstances::stream_shader; nothing bound: 1) at its
// position (TEXCOORD4.xyz, the particle's), its NDC over the draw's share below 100% (the game's offset / scale stays its
// 100% one; view_share patched the game's bytes there, not this stripped copy). At 100% with nothing bound the game's
// result: WARP tools\tw\troy\fires.
#include "../../twattila/rt_local_lights.hlsli"
#include "../../twattila/render_scale.hlsli"

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
  float g_overlay_palette_alpha : packoffset(c47.z);
  float g_debug_tonemapping : packoffset(c47.w);
  float4 g_blood_remap : packoffset(c48);
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
  float2 g_cloud_shadow_direction : packoffset(c15);
  float g_cloud_shadow_speed : packoffset(c15.z);
  float g_cloud_shadow_scale : packoffset(c15.w);
  float2 g_cloud_shadow_lerp : packoffset(c16);
  float2 g_noise_uv_shift : packoffset(c16.z);
  float2 g_skin_curvature_scale_bias : packoffset(c17);
  float2 g_skin_translucency_scale_bias : packoffset(c17.z);
  float3 g_skin_blood_colour : packoffset(c18);
  float4 g_world_bounds : packoffset(c19);
  float4 g_water_plane_bounds : packoffset(c20);
  float4 g_playable_area_bounds : packoffset(c21);
  float g_vegetation_wrap_lighting_bias : packoffset(c22);
  float g_spherical_harmonic_terms : packoffset(c22.y);
  float g_spherical_harmonic_fadeout : packoffset(c22.z);
  float g_debug_white_diffuse : packoffset(c22.w);
  float g_debug_light_diffuse_coef : packoffset(c23);
  float g_debug_light_ambient_coef : packoffset(c23.y);
  float g_debug_light_specular_coef : packoffset(c23.z);
  float g_debug_light_shadow_coef : packoffset(c23.w);
  float g_light_vegetation_backscattering_coef : packoffset(c24);
  float g_debug_light_bs_coef2 : packoffset(c24.y);
  float g_borders_colour_scale : packoffset(c24.z);
  float g_water_caustics_scale : packoffset(c24.w);
  float g_water_specular_scale : packoffset(c25);
  float g_campaign_water_attrition_strength : packoffset(c25.y);
  float g_dynamic_light_scale : packoffset(c25.z);
  float4 g_skybox_cylinder_params : packoffset(c26);
  float4 g_fog_cell_shading : packoffset(c27);
  float2 g_aristeia_pos : packoffset(c28);
  float4 g_aristeia_effect : packoffset(c29);
  float g_campaign_flat_map : packoffset(c30);
  bool g_interactive_water_enabled : packoffset(c30.y);
  float4 g_interactive_water_bounds : packoffset(c31);
  float4 g_rain_camera_position : packoffset(c32);
  float4 g_rain_camera_mat_col1 : packoffset(c33);
  float4 g_rain_camera_mat_col2 : packoffset(c34);
  float4 g_rain_camera_mat_col3 : packoffset(c35);
  float4 g_rain_direction : packoffset(c36);
  float4 g_rain_color : packoffset(c37);
  float4 g_rain_lightning : packoffset(c38);
  float4 g_rain_puddles_params : packoffset(c39);
}

cbuffer cb_vfx : register(b2)
{
  int4 g_draw_flags : packoffset(c0);
  float4 g_viewport_offset_scale : packoffset(c1);
}

Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  uint4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float4 v8 : TEXCOORD6,
  float4 v9 : TEXCOORD7,
  float3 v10 : TEXCOORD8,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_vpos_texel_offset + v0.xy;
  r0.zw = g_screen_size.zw * r0.xy;
  gbuffer_channel_1_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r0.zw = r1.zw * r0.zw;
  r0.zw = floor(r0.zw);
  r0.zw = (int2)r0.zw;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r0.zw = max(int2(0,0), (int2)r0.zw);
  r1.xy = min((int2)r0.zw, (int2)r1.xy);
  r1.zw = float2(0,0);
  r2.xyz = gbuffer_channel_1_texture.Load(r1.xyw).xyz;
  r3.xyz = gbuffer_channel_2_texture.Load(r1.xyw).xyz;
  r4.xyzw = gbuffer_channel_3_texture.Load(r1.xyw).xyzw;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r0.zw = r2.xy * float2(2,2) + float2(-1,-1);
  r2.x = dot(r0.zw, r0.zw);
  r2.x = 1 + -r2.x;
  r2.x = max(0, r2.x);
  r5.y = r2.x * 2 + -1;
  r0.zw = r0.zw + r0.zw;
  r2.x = sqrt(r2.x);
  r5.xz = r2.xx * r0.zw;
  r0.z = 255 * r4.w;
  r0.z = round(r0.z);
  r0.z = (uint)r0.z;
  r0.zw = (int2)r0.zz & int2(128,2);
  r4.z = r0.z ? r4.x : r4.z;
  r2.xyw = v1.xyz * v1.www;
  r2.xyw = float3(2500,2500,2500) * r2.xyw;
  r0.xy = r0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.xy = r0.xy / g_viewport_offset_scale.zw;
  r1.xy = (r0.xy / DrawRenderShare()) * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r0.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r0.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r0.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r1.x = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r0.xyz = r0.xyz / r1.xxx;
  r1.xyz = v6.xyz + -r0.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r3.w = rsqrt(r1.w);
  r6.xyz = r3.www * r1.xyz;
  r1.w = sqrt(r1.w);
  r1.w = saturate(r1.w / v4.x);
  r1.w = 1 + -r1.w;
  r4.w = dot(r5.xyz, r6.xyz);
  r7.xyz = r5.xyz * r4.www + -r6.xyz;
  r4.w = dot(r7.xyz, r7.xyz);
  r4.w = rsqrt(r4.w);
  r7.xyz = r7.xyz * r4.www;
  r5.xyz = r0.www ? r7.xyz : r5.xyz;
  r0.w = dot(r5.xyz, r6.xyz);
  r4.w = max(0, r0.w);
  r0.w = cmp(r0.w >= 0);
  if (r0.w != 0) {
    r7.xyz = -camera_position.xyz + r0.xyz;
    r0.y = dot(r7.xyz, r7.xyz);
    r0.y = rsqrt(r0.y);
    r7.xyz = r7.xyz * r0.yyy;
    r0.y = dot(r7.xyz, r5.xyz);
    r0.w = r0.y + r0.y;
    r5.xyz = r5.xyz * -r0.www + r7.xyz;
    r0.w = 1 + -r2.z;
    r2.z = r0.w * r0.w;
    r5.w = r2.z * r0.w;
    r6.w = r5.w * r0.w;
    r7.w = r6.w * r0.w;
    r6.w = 64.5780029 * r6.w;
    r6.w = r7.w * 28.0900002 + -r6.w;
    r5.w = r5.w * 48.6290016 + r6.w;
    r2.z = -r2.z * 12.6590004 + r5.w;
    r0.w = saturate(r0.w * 1.54589999 + r2.z);
    r0.w = 1 + -r0.w;
    r2.z = dot(r6.xyz, r5.xyz);
    r2.z = max(-1, r2.z);
    r2.z = min(1, r2.z);
    r5.x = 1 + -abs(r2.z);
    r5.x = sqrt(r5.x);
    r5.y = abs(r2.z) * -0.0187292993 + 0.0742610022;
    r5.y = r5.y * abs(r2.z) + -0.212114394;
    r5.y = r5.y * abs(r2.z) + 1.57072878;
    r5.z = r5.y * r5.x;
    r5.z = r5.z * -2 + 3.14159274;
    r2.z = cmp(r2.z < -r2.z);
    r2.z = r2.z ? r5.z : 0;
    r2.z = r5.y * r5.x + r2.z;
    r5.x = r0.w * -1.12524891 + 1.12839997;
    r5.yzw = float3(-0.00872664619,0.00872664619,0.00872664619) + r2.zzz;
    r2.z = 1 / r5.x;
    r5.xyz = r5.yzw * r2.zzz;
    r5.yw = float2(0.707106769,0.707106769) * r5.xy;
    r6.x = abs(r5.y) * abs(r5.y);
    r6.y = r6.x * abs(r5.y);
    r6.z = r6.y * abs(r5.y);
    r5.y = abs(r5.y) * 0.278393 + 1;
    r5.y = r6.x * 0.230388999 + r5.y;
    r5.y = r6.y * 0.000972000009 + r5.y;
    r5.y = r6.z * 0.0781079978 + r5.y;
    r5.y = r5.y * r5.y;
    r5.y = r5.y * r5.y;
    r5.y = 1 / r5.y;
    r5.y = 1 + -r5.y;
    r6.x = cmp(0 < r5.x);
    r5.x = cmp(r5.x < 0);
    r5.x = (int)-r6.x + (int)r5.x;
    r5.x = (int)r5.x;
    r5.x = r5.x * r5.y + 1;
    r5.x = 0.5 * r5.x;
    r5.y = r5.w * r5.w;
    r6.x = r5.y * r5.w;
    r5.w = r6.x * r5.w;
    r5.z = r5.z * 0.196853578 + 1;
    r5.y = r5.y * 0.230388999 + r5.z;
    r5.y = r6.x * 0.000972000009 + r5.y;
    r5.y = r5.w * 0.0781079978 + r5.y;
    r5.y = r5.y * r5.y;
    r5.y = r5.y * r5.y;
    r5.y = 1 / r5.y;
    r5.y = 2 + -r5.y;
    r5.x = r5.y * 0.5 + -r5.x;
    r5.yz = float2(-1.11072075,1.11072075) * r2.zz;
    r6.xy = r5.yz * r5.yz;
    r5.w = r6.x * -r5.y;
    r5.y = r5.w * -r5.y;
    r2.z = r2.z * 0.309216887 + 1;
    r6.xz = r6.xy * float2(0.230388999,0.230388999) + r2.zz;
    r2.z = r5.w * 0.000972000009 + r6.x;
    r2.z = r5.y * 0.0781079978 + r2.z;
    r2.z = r2.z * r2.z;
    r2.z = r2.z * r2.z;
    r2.z = 1 / r2.z;
    r2.z = 1 + -r2.z;
    r2.z = 1 + -r2.z;
    r2.z = 0.5 * r2.z;
    r5.y = r6.y * r5.z;
    r5.z = r5.y * r5.z;
    r5.y = r5.y * 0.000972000009 + r6.z;
    r5.y = r5.z * 0.0781079978 + r5.y;
    r5.y = r5.y * r5.y;
    r5.y = r5.y * r5.y;
    r5.y = 1 / r5.y;
    r5.y = 2 + -r5.y;
    r2.z = r5.y * 0.5 + -r2.z;
    r2.z = abs(r5.x) / r2.z;
    r2.z = min(1, r2.z);
    r2.z = r2.z * r2.z;
    r2.z = 0.785398185 * r2.z;
    r0.w = 1 + -r0.w;
    r5.x = min(1, r4.w);
    r5.y = 1.57079637 * r0.w;
    r5.y = sin(r5.y);
    r5.z = -1 + r5.x;
    r5.y = r5.y * r5.z + 1;
    r0.y = saturate(r0.y);
    r0.w = r0.w * r0.w;
    r5.z = 0.5 * r0.w;
    r0.w = -r0.w * 0.5 + 1;
    r5.x = r5.x * r0.w + r5.z;
    r5.x = 1 / r5.x;
    r0.y = r0.y * r0.w + r5.z;
    r0.y = 1 / r0.y;
    r0.y = r5.x * r0.y;
    r0.y = 0.25 * r0.y;
    r0.y = min(1, r0.y);
    r0.y = r0.y * r5.y;
    r1.xyz = r1.xyz * r3.www + -r7.xyz;
    r0.w = dot(r1.xyz, r1.xyz);
    r0.w = rsqrt(r0.w);
    r1.xyz = r1.xyz * r0.www;
    r0.w = saturate(dot(-r7.xyz, r1.xyz));
    r1.x = r0.w * -5.55472994 + -6.98316002;
    r0.w = r1.x * r0.w;
    r0.w = exp2(r0.w);
    r1.xyz = float3(1,1,1) + -r4.xyz;
    r1.xyz = r1.xyz * r0.www + r4.xyz;
    r0.y = r2.z * r0.y;
    r1.xyz = r0.yyy * r1.xyz;
  } else {
    r1.xyz = float3(0,0,0);
  }
  r5.xyz = r1.xyz * r2.xyw;
  r1.xyz = max(r4.xyz, r1.xyz);
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r2.xyz = r3.xyz * r2.xyw;
  r1.xyz = r2.xyz * r1.xyz;
  r1.xyz = r1.xyz * r4.www;
  r1.xyz = g_debug_light_diffuse_coef * r1.xyz;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  r0.y = saturate(1 + g_debug_light_shadow_coef);
  r0.w = g_aristeia_effect.x * g_aristeia_effect.x;
  r0.xz = -g_aristeia_pos.xy + r0.xz;
  r0.x = dot(r0.xz, r0.xz);
  r0.x = r0.x / r0.w;
  r0.x = min(1, r0.x);
  r0.z = g_aristeia_effect.w + -g_aristeia_effect.z;
  r0.x = r0.x * r0.z + g_aristeia_effect.z;
  r0.x = -1 + r0.x;
  r0.z = g_aristeia_effect.y * r0.x;
  r0.x = g_aristeia_effect.y * r0.x + 1;
  r0.z = saturate(1.5 * r0.z);
  r0.w = 1 + -r0.y;
  r0.y = r0.z * r0.w + r0.y;
  r0.x = r0.x * r0.y;
  r0.yzw = r5.xyz * g_debug_light_specular_coef + r1.xyz;
  r0.xyz = r0.yzw * r0.xxx;
  r0.xyz = r1.www * r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v6.xyz);
  o0.w = 1;
  return;
}