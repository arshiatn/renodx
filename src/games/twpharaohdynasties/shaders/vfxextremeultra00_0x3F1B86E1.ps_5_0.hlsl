// Reconstructed from the supplied 0x3F1B86E1.ps_5_0.cso (Extreme VFX).
// Entry: main, target: ps_5_0. Compilation/game testing is still required.
// Temporary registers hold uint bits, matching DXBC's untyped registers.
// asfloat/asint reinterpret bits; explicit casts only perform real conversions.
// Original named buffer layouts, resource slots, sampling and flow are retained.
// Trailing numbers identify original SHEX DWORD offsets for inspection.
//
// Integrate your existing slider by changing PharaohVfxBrightness below.
// The neutral value 1.0 preserves the intended stock particle brightness.
// Do not multiply the final RGBA output: alpha and added fog must stay intact.

#include "../shared.h"

// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 22 06:54:30 2026

struct EMITTER_CONSTANTS_PS
{
    uint m_behaviour_mask;         // Offset:    0
    uint m_diffuse_id;             // Offset:    4
    uint m_normal_id;              // Offset:    8
    uint m_draw_flags;             // Offset:   12
    float m_ps_receive_shadows_high_quality_attenuation;// Offset:   16
    float m_ps_receive_shadows_high_quality_shadow_volume_depth_scaler;// Offset:   20
    float m_ps_lighting_emissive;  // Offset:   24
    float m_ps_lighting_directionality;// Offset:   28
    float m_ps_lighting_average_size;// Offset:   32
    float m_ps_lighting_additive_blend_mul;// Offset:   36
    float m_ps_lighting_small_particles;// Offset:   40
    float m_ps_lighting_large_particles;// Offset:   44
    uint m_ps_alpha_crush_sharp_transition;// Offset:   48
    float m_ps_alpha_crush_alpha_crush_min;// Offset:   52
    float m_ps_alpha_crush_alpha_crush_max;// Offset:   56
    float m_ps_alpha_crush_variation;// Offset:   60
    float m_ps_soft_edge_soft_factor;// Offset:   64
    uint m_ps_texture_overlay_colour;// Offset:   68
    uint m_ps_legacy_lighting;     // Offset:   72
    float m_mem_padding;           // Offset:   76
};

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  column_major float4x4 view : packoffset(c1);
  column_major float4x4 projection : packoffset(c5);
  column_major float4x4 view_projection : packoffset(c9);
  column_major float4x4 inv_view : packoffset(c13);
  column_major float4x4 inv_projection : packoffset(c17);
  column_major float4x4 inv_view_projection : packoffset(c21);
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

cbuffer shadowmap_PS : register(b1)
{
  float4 g_vHardShadowBufferSize : packoffset(c0);
  column_major float4x4 g_amHardSplit[4] : packoffset(c1);
  column_major float4x4 g_amHardSplit_prewarp[4] : packoffset(c17);
  float2 g_fFadeRange : packoffset(c33);
  float g_sample_bias : packoffset(c33.z);
  float g_iSplitCount : packoffset(c33.w);
  float3 g_shadow_light_direction : packoffset(c34);
  float g_split_distances[5] : packoffset(c35);
  float4 g_split_uv_texture_bounds[4] : packoffset(c40);
  float4 g_vShadowRange : packoffset(c44);
  float3 g_vBakedShadowmapScale : packoffset(c45);
  float3 g_vBakedShadowmapOffset : packoffset(c46);
  float2 g_vBakedShadowmapShear : packoffset(c47);
  float4 g_vBakedShadowmapParams : packoffset(c48);
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

cbuffer cb_vfx : register(b5)
{
  int4 g_draw_flags : packoffset(c0);
  float4 g_viewport_offset_scale : packoffset(c1);
}

SamplerState s_sky_s : register(s0);
SamplerState g_sam_diffuse_s : register(s3);
SamplerState g_sam_normal_s : register(s4);
SamplerComparisonState sBakedShadowBuffer_s : register(s1);
SamplerComparisonState sShadowBuffer_SM50_s : register(s2);
TextureCube<float4> t_sky : register(t0);
Texture2D<float4> g_tBakedShadowBuffer : register(t1);
Texture2D<float4> g_tBakedShadowDirs : register(t2);
Texture2D<float4> g_tShadowBuffer_SM50 : register(t3);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t4);
Texture2D<float4> gbuffer_channel_4_texture : register(t5);
Texture2DArray<float4> g_tex_diffuse : register(t6);
Texture2DArray<float4> g_tex_normal : register(t7);
StructuredBuffer<EMITTER_CONSTANTS_PS> g_constant_buffer_ps : register(t8);


// Slider hook: replace 1.0f with your actual SI brightness expression.
// Values are LINEAR multipliers: 1.0 = neutral, 2.0 = twice the particle RGB.
// emitterIndex is available if your own classification selects several sliders.
// No SI field names or particle-type classification have been invented here.
float PharaohVfxBrightness(uint emitterIndex)
{
  return HDR>=0.5 ? VfxBaseBrightness : 1;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  nointerpolation uint4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  nointerpolation float4 v7 : TEXCOORD5,
  nointerpolation float4 v8 : TEXCOORD6,
  nointerpolation float4 v9 : TEXCOORD7,
  float3 v10 : TEXCOORD8,
  out float4 o0 : SV_Target0)
{
  // Raw 32-bit register storage: retain masks and packed values exactly.
  uint4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18;

  r0.xyzw = uint4(g_constant_buffer_ps[v2.x].m_behaviour_mask, g_constant_buffer_ps[v2.x].m_diffuse_id, g_constant_buffer_ps[v2.x].m_normal_id, g_constant_buffer_ps[v2.x].m_draw_flags); // 114: ld_structured
  r1.xyzw = uint4(asuint(g_constant_buffer_ps[v2.x].m_ps_receive_shadows_high_quality_attenuation), asuint(g_constant_buffer_ps[v2.x].m_ps_receive_shadows_high_quality_shadow_volume_depth_scaler), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_emissive), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_directionality)); // 125: ld_structured
  r2.xyzw = uint4(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_sharp_transition, asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_alpha_crush_min), asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_alpha_crush_max), asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_variation)); // 136: ld_structured
  r3.xyz = uint3(asuint(g_constant_buffer_ps[v2.x].m_ps_soft_edge_soft_factor), g_constant_buffer_ps[v2.x].m_ps_texture_overlay_colour, g_constant_buffer_ps[v2.x].m_ps_legacy_lighting); // 147: ld_structured
  r4.xyzw = r0.xxxx  & uint4(0x00010000u, 0x00100000u, 0x00200000u, 0x00020000u); // 158: and
  if (r4.x != 0u) { // 168: if
    r5.z = asuint((float)(r0.y)); // 171: utof
    r5.xy = asuint(v3.xy); // 176: mov
    r6.xyzw = asuint(g_tex_diffuse.Sample(g_sam_diffuse_s, asfloat(r5.xyz)).xyzw); // 181: sample
    r5.xy = asuint(v5.xy); // 192: mov
    r5.xyzw = asuint(g_tex_diffuse.Sample(g_sam_diffuse_s, asfloat(r5.xyz)).xyzw); // 197: sample
    r5.xyzw = asuint(-(asfloat(r6.xyzw)) + asfloat(r5.xyzw)); // 208: add
    r5.xyzw = asuint(mad(v5.zzzz, asfloat(r5.xyzw), asfloat(r6.xyzw))); // 216: mad
    r0.y = (0.0404499993f >= asfloat(r5.w)) ? 0xffffffffu : 0u; // 225: ge
    r3.w = asuint(asfloat(r5.w) * 0.0773993805f); // 232: mul
    r4.x = asuint(asfloat(r5.w) + 0.0549999997f); // 239: add
    r4.x = asuint(asfloat(r4.x) * 0.947867334f); // 246: mul
    r4.x = asuint(log2(asfloat(r4.x))); // 253: log
    r4.x = asuint(asfloat(r4.x) * 2.4000001f); // 258: mul
    r4.x = asuint(exp2(asfloat(r4.x))); // 265: exp
    r0.y = (r0.y != 0u) ? r3.w : r4.x; // 270: movc
    r6.xyz = asuint(min(asfloat(r0.yyy), asfloat(r5.xyz))); // 279: min
    r3.w = asuint(dot(v1.xyz, float3(0.212599993f, 0.715200007f, 0.0722000003f))); // 286: dp3
    r7.xyz = asuint(v1.xyz / asfloat(r3.www)); // 296: div
    r4.x = (asfloat(r0.y) < 0.5f) ? 0xffffffffu : 0u; // 303: lt
    r8.xyz = asuint(asfloat(r6.xyz) * asfloat(r7.xyz)); // 310: mul
    r8.xyz = asuint(asfloat(r8.xyz) + asfloat(r8.xyz)); // 317: add
    r9.xyz = asuint(-(asfloat(r6.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 324: add
    r9.xyz = asuint(asfloat(r9.xyz) + asfloat(r9.xyz)); // 335: add
    r10.xyz = asuint(-(asfloat(r7.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 342: add
    r9.xyz = asuint(mad(-(asfloat(r9.xyz)), asfloat(r10.xyz), float3(1.0f, 1.0f, 1.0f))); // 353: mad
    r5.w = r4.x  & 0x3f800000u; // 366: and
    r4.x = (r4.x != 0u) ? 0x00000000u : 0x3f800000u; // 373: movc
    r9.xyz = asuint(asfloat(r9.xyz) * asfloat(r4.xxx)); // 382: mul
    r8.xyz = asuint(mad(asfloat(r5.www), asfloat(r8.xyz), asfloat(r9.xyz))); // 389: mad
    r8.xyz = asuint(asfloat(r3.www) * asfloat(r8.xyz)); // 398: mul
    r6.xyz = asuint(asfloat(r6.xyz) * v1.xyz); // 405: mul
    r6.xyz = (r3.yyy != uint3(0u, 0u, 0u)) ? r8.xyz : r6.xyz; // 412: movc
    r6.xyz = asuint(asfloat(r6.xyz) * v1.www); // 421: mul
    r6.w = asuint(saturate(asfloat(r0.y) * v1.w)); // 428: mul
    r5.xyz = asuint(saturate(-(asfloat(r0.yyy)) + asfloat(r5.xyz))); // 435: add
    r8.xyz = (asfloat(r5.xyz) < float3(0.5f, 0.5f, 0.5f)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 443: lt
    r7.xyz = asuint(asfloat(r5.xyz) * asfloat(r7.xyz)); // 453: mul
    r7.xyz = asuint(asfloat(r7.xyz) + asfloat(r7.xyz)); // 460: add
    r9.xyz = asuint(-(asfloat(r5.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 467: add
    r9.xyz = asuint(asfloat(r9.xyz) * asfloat(r10.xyz)); // 478: mul
    r9.xyz = asuint(mad(-(asfloat(r9.xyz)), float3(2.0f, 2.0f, 2.0f), float3(1.0f, 1.0f, 1.0f))); // 485: mad
    r10.xyz = r8.xyz  & uint3(0x3f800000u, 0x3f800000u, 0x3f800000u); // 501: and
    r8.xyz = (r8.xyz != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : uint3(0x3f800000u, 0x3f800000u, 0x3f800000u); // 511: movc
    r8.xyz = asuint(asfloat(r9.xyz) * asfloat(r8.xyz)); // 526: mul
    r7.xyz = asuint(mad(asfloat(r10.xyz), asfloat(r7.xyz), asfloat(r8.xyz))); // 533: mad
    r7.xyz = asuint(asfloat(r3.www) * asfloat(r7.xyz)); // 542: mul
    r5.xyz = asuint(asfloat(r5.xyz) * v1.xyz); // 549: mul
    r5.xyz = (r3.yyy != uint3(0u, 0u, 0u)) ? r7.xyz : r5.xyz; // 556: movc
    r5.xyz = asuint(asfloat(r5.xyz) * v1.www); // 565: mul
  } else { // 572: else
    r6.xyzw = asuint(v1.xyzw); // 573: mov
    r5.xyz = uint3(0x00000000u, 0x00000000u, 0x00000000u); // 578: mov
  } // 586: endif
  r0.y = (asfloat(r2.y) == asfloat(r2.z)) ? 0xffffffffu : 0u; // 587: eq
  r2.w = asuint(-(asfloat(r2.w)) + 1.0f); // 594: add
  r3.y = asuint(max(asfloat(r2.w), asfloat(r2.y))); // 602: max
  r7.xy = asuint(v4.ww * float2(3276.0f, 4099.0f)); // 609: mul
  r7.xy = asuint(frac(asfloat(r7.xy))); // 619: frc
  r3.y = asuint(asfloat(r3.y) + -1.0f); // 624: add
  r3.y = asuint(mad(asfloat(r7.x), asfloat(r3.y), 1.0f)); // 631: mad
  r2.w = asuint(asfloat(r2.w) * asfloat(r2.z)); // 640: mul
  r2.w = asuint(max(asfloat(r2.w), asfloat(r2.y))); // 647: max
  r2.w = asuint(-(asfloat(r2.z)) + asfloat(r2.w)); // 654: add
  r2.z = asuint(mad(asfloat(r7.y), asfloat(r2.w), asfloat(r2.z))); // 662: mad
  r0.y = (r0.y != 0u) ? r3.y : r2.z; // 671: movc
  r0.y = asuint(-(asfloat(r2.y)) + asfloat(r0.y)); // 680: add
  r0.y = asuint(max(asfloat(r0.y), 0.00100000005f)); // 688: max
  r2.y = asuint(-(asfloat(r2.y)) + v6.w); // 695: add
  r0.y = asuint(saturate(asfloat(r2.y) / asfloat(r0.y))); // 703: div
  r2.y = asuint(saturate(-(asfloat(r0.y)) + asfloat(r6.w))); // 710: add
  r2.z = asuint(-(asfloat(r0.y)) + 1.00100005f); // 718: add
  r2.y = asuint(asfloat(r2.y) / asfloat(r2.z)); // 726: div
  r0.y = asuint(-(asfloat(r0.y)) + asfloat(r2.y)); // 733: add
  r0.y = (asfloat(r0.y) >= 0.0f) ? 0xffffffffu : 0u; // 741: ge
  r0.y = r0.y  & 0x3f800000u; // 748: and
  r0.y = asuint(asfloat(r2.y) * asfloat(r0.y)); // 755: mul
  r2.w = (r2.x != 0u) ? r0.y : r2.y; // 762: movc
  r0.y = (asfloat(r6.w) != 0.0f) ? 0xffffffffu : 0u; // 771: ne
  r3.y = asuint(asfloat(r2.w) / asfloat(r6.w)); // 778: div
  r0.y = (r0.y != 0u) ? r3.y : 0x3f800000u; // 785: movc
  r2.xyz = asuint(asfloat(r0.yyy) * asfloat(r6.xyz)); // 794: mul
  r2.xyzw = (r4.yyyy != uint4(0u, 0u, 0u, 0u)) ? r2.xyzw : r6.xyzw; // 801: movc
  if (r4.z != 0u) { // 810: if
    r6.xyz = asuint(v6.xyz); // 813: mov
    r6.w = 0x3f800000u; // 818: mov
    r0.y = asuint(dot(asfloat(r6.xyzw), float4(view._m02, view._m12, view._m22, view._m32))); // 823: dp4
    r3.yw = asuint(v0.xy + float2(g_vpos_texel_offset, g_vpos_texel_offset)); // 831: add
    r3.yw = asuint(asfloat(r3.yw) * float2(g_render_target_dimensions.z, g_render_target_dimensions.w)); // 839: mul
    r4.x = (1 < g_num_of_samples) ? 0xffffffffu : 0u; // 847: ilt
    if (r4.x != 0u) { // 855: if
      uint4 dimensions_858 = uint4(0u, 0u, 0u, 0u);
      gbuffer_channel_4_texture_ms.GetDimensions(dimensions_858.x, dimensions_858.y, dimensions_858.w);
      r4.yz = dimensions_858.xy; // 858: resinfo
    } else { // 867: else
      uint4 dimensions_868 = uint4(0u, 0u, 0u, 0u);
      gbuffer_channel_4_texture.GetDimensions(0x00000000u, dimensions_868.x, dimensions_868.y, dimensions_868.w);
      r4.yz = dimensions_868.xy; // 868: resinfo
    } // 877: endif
    r6.xy = asuint((float2)(r4.yz)); // 878: utof
    r6.xy = asuint(asfloat(r3.yw) * asfloat(r6.xy)); // 883: mul
    r6.xy = asuint(floor(asfloat(r6.xy))); // 890: round_ni
    r6.xy = asuint((int2)(asfloat(r6.xy))); // 895: ftoi
    r4.yz = asuint(asint(r4.yz) + int2(-1, -1)); // 900: iadd
    r6.xy = asuint(max(asint(r6.xy), int2(0, 0))); // 910: imax
    r6.xy = asuint(min(asint(r4.yz), asint(r6.xy))); // 920: imin
    if (r4.x != 0u) { // 927: if
      r6.z = 0x00000000u; // 930: mov
      r7.z = asuint(gbuffer_channel_4_texture_ms.Load(asint(r6.xy), 0).x); // 935: ld_ms
    } else { // 946: else
      r6.w = 0x00000000u; // 947: mov
      r7.z = asuint(gbuffer_channel_4_texture.Load(asint(r6.xyw)).x); // 952: ld
    } // 961: endif
    r7.xy = asuint(mad(asfloat(r3.yw), float2(2.0f, -2.0f), float2(-1.0f, 1.0f))); // 962: mad
    r7.w = 0x3f800000u; // 977: mov
    r3.y = asuint(dot(asfloat(r7.xyzw), float4(inv_projection._m02, inv_projection._m12, inv_projection._m22, inv_projection._m32))); // 982: dp4
    r3.w = asuint(dot(asfloat(r7.xyzw), float4(inv_projection._m03, inv_projection._m13, inv_projection._m23, inv_projection._m33))); // 990: dp4
    r3.y = asuint(asfloat(r3.y) / asfloat(r3.w)); // 998: div
    r0.y = asuint(-(asfloat(r0.y)) + asfloat(r3.y)); // 1005: add
    r0.y = asuint(saturate(asfloat(r0.y) / asfloat(r3.x))); // 1013: div
    r2.w = asuint(asfloat(r0.y) * asfloat(r2.w)); // 1020: mul
  } else { // 1027: else
    r0.y = 0x3f800000u; // 1028: mov
  } // 1033: endif
  if (r4.w != 0u) { // 1034: if
    r4.z = asuint((float)(r0.z)); // 1037: utof
    r4.xy = asuint(v3.xy); // 1042: mov
    r3.xyw = asuint(g_tex_normal.Sample(g_sam_normal_s, asfloat(r4.xyz)).xyz); // 1047: sample
    r3.xyw = asuint(mad(asfloat(r3.xyw), float3(2.0f, 2.0f, 2.0f), float3(-1.0f, -1.0f, -1.0f))); // 1058: mad
    r0.z = asuint(dot(asfloat(r3.xyw), asfloat(r3.xyw))); // 1073: dp3
    r0.z = asuint(rsqrt(asfloat(r0.z))); // 1080: rsq
    r3.xyw = asuint(asfloat(r0.zzz) * asfloat(r3.xyw)); // 1085: mul
  } else { // 1092: else
    r3.xyw = uint3(0x00000000u, 0x00000000u, 0x3f800000u); // 1093: mov
  } // 1101: endif
  r0.xz = r0.xx  & uint2(0x00040000u, 0x00080000u); // 1102: and
  if (r0.x != 0u) { // 1112: if
    r4.xyz = asuint(-(v6.xyz) + float3(camera_position.x, camera_position.y, camera_position.z)); // 1115: add
    r0.x = asuint(dot(asfloat(r4.xyz), asfloat(r4.xyz))); // 1124: dp3
    r0.x = asuint(rsqrt(asfloat(r0.x))); // 1131: rsq
    r4.xyz = asuint(asfloat(r0.xxx) * asfloat(r4.xyz)); // 1136: mul
    r0.x = asuint(min(v4.y, v4.x)); // 1143: min
    r0.x = asuint(asfloat(r1.y) * asfloat(r0.x)); // 1150: mul
    r6.xyzw = asuint(asfloat(r0.xxxx) * float4(0.166666672f, 0.333333343f, 0.5f, 0.666666687f)); // 1157: mul
    r7.xyz = asuint(v6.xyz); // 1167: mov
    r7.w = 0x3f800000u; // 1172: mov
    r1.y = asuint(dot(asfloat(r7.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 1177: dp4
    r7.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), v6.xyz)); // 1185: mad
    r4.w = asuint(g_iSplitCount + -1.0f); // 1197: add
    r5.w = (asfloat(r1.y) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 1205: lt
    if (r5.w != 0u) { // 1213: if
      r8.x = asuint(g_split_distances[1]); // 1216: mov
      r8.y = asuint(g_split_distances[2]); // 1222: mov
      r8.z = asuint(g_split_distances[3]); // 1228: mov
      r8.w = asuint(g_split_distances[4]); // 1234: mov
      r8.xyzw = (asfloat(r1.yyyy) >= asfloat(r8.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 1240: ge
      r8.xyzw = r8.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 1247: and
      r5.w = asuint(dot(asfloat(r8.xyzw), asfloat(r8.xyzw))); // 1257: dp4
      r5.w = asuint(asfloat(r5.w) + 0.5f); // 1264: add
      r5.w = asuint(floor(asfloat(r5.w))); // 1271: round_ni
      r5.w = asuint(min(asfloat(r4.w), asfloat(r5.w))); // 1276: min
      r8.x = asuint((int)(asfloat(r5.w))); // 1283: ftoi
      r8.y = r8.x << (0x00000002u & 31u); // 1288: ishl
      r7.w = 0x3f800000u; // 1295: mov
      r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.y / 4u)]._m00, g_amHardSplit[(r8.y / 4u)]._m10, g_amHardSplit[(r8.y / 4u)]._m20, g_amHardSplit[(r8.y / 4u)]._m30))); // 1300: dp4
      r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.y / 4u)]._m01, g_amHardSplit[(r8.y / 4u)]._m11, g_amHardSplit[(r8.y / 4u)]._m21, g_amHardSplit[(r8.y / 4u)]._m31))); // 1310: dp4
      r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.y / 4u)]._m02, g_amHardSplit[(r8.y / 4u)]._m12, g_amHardSplit[(r8.y / 4u)]._m22, g_amHardSplit[(r8.y / 4u)]._m32))); // 1320: dp4
      r8.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.y / 4u)]._m03, g_amHardSplit[(r8.y / 4u)]._m13, g_amHardSplit[(r8.y / 4u)]._m23, g_amHardSplit[(r8.y / 4u)]._m33))); // 1330: dp4
      r8.z = asuint(1.0f / asfloat(r8.z)); // 1340: div
      r10.xyz = asuint(asfloat(r8.zzz) * asfloat(r9.xyz)); // 1350: mul
      r8.w = asuint(g_iSplitCount * 0.5f); // 1357: mul
      r11.y = asuint(ceil(asfloat(r8.w))); // 1365: round_pi
      r11.x = 0x40000000u; // 1370: mov
      r8.w = r8.x  & 0x00000001u; // 1375: and
      r9.z = asuint(asint(r8.x) >> (0x00000001u & 31u)); // 1382: ishr
      r9.z = r9.z  & 0x00000001u; // 1389: and
      r12.x = asuint((float)(asint(r8.w))); // 1396: itof
      r12.y = asuint((float)(asint(r9.z))); // 1401: itof
      r9.zw = asuint(mad(asfloat(r10.xy), asfloat(r11.xy), -(asfloat(r12.xy)))); // 1406: mad
      r9.zw = asuint(mad(asfloat(r9.zw), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 1416: mad
      r10.xy = asuint(saturate(asfloat(r9.zw))); // 1431: mov
      r9.zw = (asfloat(r9.zw) == asfloat(r10.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 1436: eq
      r5.w = asuint(trunc(asfloat(r5.w))); // 1443: round_z
      r5.w = (asfloat(r4.w) == asfloat(r5.w)) ? 0xffffffffu : 0u; // 1448: eq
      r8.w = r9.w  & r9.z; // 1455: and
      r9.z = (asint(r5.w) == -1) ? 0xffffffffu : 0u; // 1462: ieq
      r8.w = r8.w  | r9.z; // 1469: or
      if (r8.w != 0u) { // 1476: if
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 1479: mul
        r12.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r11.xyzw))); // 1490: mad
        r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r10.z))); // 1499: sample_c
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r10.z))); // 1512: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1525: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 1532: mul
        r13.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r12.xyzw))); // 1543: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r10.z))); // 1552: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1565: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r10.z))); // 1572: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1585: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 1592: mul
        r14.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r13.xyzw))); // 1603: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r10.z))); // 1612: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1625: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r10.z))); // 1632: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1645: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 1652: mul
        r15.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r14.xyzw))); // 1663: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r10.z))); // 1672: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1685: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r10.z))); // 1692: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1705: add
        r15.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 1712: mul
        r16.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r15.xyzw))); // 1723: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.xy), asfloat(r10.z))); // 1732: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1745: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.zw), asfloat(r10.z))); // 1752: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 1765: add
        r16.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 1772: mul
        r17.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.zzzz), asfloat(r16.xyzw))); // 1783: mad
        r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r17.xy), asfloat(r10.z))); // 1792: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r8.w)); // 1805: add
        r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r17.zw), asfloat(r10.z))); // 1812: sample_c
        r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 1825: add
        r8.z = asuint(asfloat(r8.z) * 0.0833333358f); // 1832: mul
        if (r5.w == 0u) { // 1839: if
          r5.w = asuint(asint(r8.y) + 4); // 1842: iadd
          r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m00, g_amHardSplit[(r5.w / 4u)]._m10, g_amHardSplit[(r5.w / 4u)]._m20, g_amHardSplit[(r5.w / 4u)]._m30))); // 1849: dp4
          r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m01, g_amHardSplit[(r5.w / 4u)]._m11, g_amHardSplit[(r5.w / 4u)]._m21, g_amHardSplit[(r5.w / 4u)]._m31))); // 1859: dp4
          r8.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m02, g_amHardSplit[(r5.w / 4u)]._m12, g_amHardSplit[(r5.w / 4u)]._m22, g_amHardSplit[(r5.w / 4u)]._m32))); // 1869: dp4
          r5.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m03, g_amHardSplit[(r5.w / 4u)]._m13, g_amHardSplit[(r5.w / 4u)]._m23, g_amHardSplit[(r5.w / 4u)]._m33))); // 1879: dp4
          r5.w = asuint(1.0f / asfloat(r5.w)); // 1889: div
          r8.w = asuint(asfloat(r5.w) * asfloat(r8.w)); // 1899: mul
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r11.xyzw))); // 1906: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 1915: sample_c
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 1928: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 1941: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r12.xyzw))); // 1948: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 1957: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 1970: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 1977: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 1990: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r13.xyzw))); // 1997: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 2006: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2019: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 2026: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2039: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r14.xyzw))); // 2046: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 2055: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2068: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 2075: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2088: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r15.xyzw))); // 2095: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 2104: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2117: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 2124: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 2137: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r16.xyzw))); // 2144: mad
          r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 2153: sample_c
          r5.w = asuint(asfloat(r5.w) + asfloat(r9.w)); // 2166: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 2173: sample_c
          r5.w = asuint(asfloat(r5.w) + asfloat(r8.w)); // 2186: add
          r8.w = asuint(asint(r8.x) + 1); // 2193: iadd
          r9.x = asuint(-(g_split_distances[r8.x]) + g_split_distances[r8.w]); // 2200: add
          r9.y = asuint(asfloat(r9.x) * g_fFadeRange.x); // 2214: mul
          r8.w = asuint(mad(-(asfloat(r9.x)), g_fFadeRange.x, g_split_distances[r8.w])); // 2222: mad
          r8.w = asuint(asfloat(r1.y) + -(asfloat(r8.w))); // 2236: add
          r8.w = asuint(saturate(asfloat(r8.w) / asfloat(r9.y))); // 2244: div
          r5.w = asuint(mad(asfloat(r5.w), 0.0833333358f, -(asfloat(r8.z)))); // 2251: mad
          r8.z = asuint(mad(asfloat(r8.w), asfloat(r5.w), asfloat(r8.z))); // 2261: mad
        } // 2270: endif
        if (r9.z != 0u) { // 2271: if
          r5.w = asuint(asint(r8.x) + 1); // 2274: iadd
          r5.w = asuint(-(g_split_distances[r8.x]) + g_split_distances[r5.w]); // 2281: add
          r8.w = asuint(-(g_fFadeRange.y) + 1.0f); // 2295: add
          r8.x = asuint(mad(asfloat(r5.w), asfloat(r8.w), g_split_distances[r8.x])); // 2304: mad
          r1.y = asuint(asfloat(r1.y) + -(asfloat(r8.x))); // 2316: add
          r5.w = asuint(asfloat(r5.w) * g_fFadeRange.y); // 2324: mul
          r1.y = asuint(saturate(asfloat(r1.y) / asfloat(r5.w))); // 2332: div
          r5.w = asuint(-(asfloat(r8.z)) + 1.0f); // 2339: add
          r8.z = asuint(mad(asfloat(r1.y), asfloat(r5.w), asfloat(r8.z))); // 2347: mad
        } // 2356: endif
      } else { // 2357: else
        r1.y = asuint(asint(r8.y) + 4); // 2358: iadd
        r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m00, g_amHardSplit[(r1.y / 4u)]._m10, g_amHardSplit[(r1.y / 4u)]._m20, g_amHardSplit[(r1.y / 4u)]._m30))); // 2365: dp4
        r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m01, g_amHardSplit[(r1.y / 4u)]._m11, g_amHardSplit[(r1.y / 4u)]._m21, g_amHardSplit[(r1.y / 4u)]._m31))); // 2375: dp4
        r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m02, g_amHardSplit[(r1.y / 4u)]._m12, g_amHardSplit[(r1.y / 4u)]._m22, g_amHardSplit[(r1.y / 4u)]._m32))); // 2385: dp4
        r1.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m03, g_amHardSplit[(r1.y / 4u)]._m13, g_amHardSplit[(r1.y / 4u)]._m23, g_amHardSplit[(r1.y / 4u)]._m33))); // 2395: dp4
        r1.y = asuint(1.0f / asfloat(r1.y)); // 2405: div
        r8.xyw = asuint(asfloat(r1.yyy) * asfloat(r9.xyz)); // 2415: mul
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r8.xyxy))); // 2422: mad
        r1.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2435: sample_c
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2448: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2461: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r8.xyxy))); // 2468: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2481: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2494: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2501: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2514: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r8.xyxy))); // 2521: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2534: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2547: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2554: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2567: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r8.xyxy))); // 2574: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2587: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2600: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2607: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2620: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r8.xyxy))); // 2627: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2640: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2653: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2660: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2673: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r8.xyxy))); // 2680: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 2693: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2706: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 2713: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 2726: add
        r8.z = asuint(asfloat(r1.y) * 0.0833333358f); // 2733: mul
      } // 2740: endif
    } else { // 2741: else
      r8.z = 0x3f800000u; // 2742: mov
    } // 2747: endif
    r8.xyw = asuint(asfloat(r7.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 2748: add
    r1.y = asuint(dot(asfloat(r8.xyw), asfloat(r8.xyw))); // 2757: dp3
    r1.y = asuint(sqrt(asfloat(r1.y))); // 2764: sqrt
    r5.w = (asfloat(r1.y) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 2769: ge
    if (r5.w != 0u) { // 2777: if
      r8.xyw = asuint(mad(asfloat(r7.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 2780: mad
      r8.xy = asuint(mad(asfloat(r7.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r8.xy))); // 2791: mad
      r5.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 2801: lt
      if (r5.w != 0u) { // 2809: if
        uint4 dimensions_2812 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_2812.x, dimensions_2812.y, dimensions_2812.w);
        r9.xy = asuint((float2)(dimensions_2812.xy)); // 2812: resinfo
        r9.zw = asuint(asfloat(r8.xy) * asfloat(r9.xy)); // 2821: mul
        r9.zw = asuint(floor(asfloat(r9.zw))); // 2828: round_ni
        r9.zw = asuint((int2)(asfloat(r9.zw))); // 2833: ftoi
        r10.xy = asuint((int2)(asfloat(r9.xy))); // 2838: ftoi
        r10.xy = asuint(asint(r10.xy) + int2(-1, -1)); // 2843: iadd
        r11.xy = r9.zw  & r10.xy; // 2853: and
        r11.zw = uint2(0x00000000u, 0x00000000u); // 2860: mov
        r12.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r11.xyw)).xyzw); // 2868: ld
        r9.zw = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r12.xy))); // 2877: mad
        r9.zw = asuint(mad(asfloat(r7.yy), asfloat(r12.zw), asfloat(r9.zw))); // 2887: mad
        r9.xy = asuint(asfloat(r9.zw) * asfloat(r9.xy)); // 2896: mul
        r9.xy = asuint(floor(asfloat(r9.xy))); // 2903: round_ni
        r9.xy = asuint((int2)(asfloat(r9.xy))); // 2908: ftoi
        r10.xy = r10.xy  & r9.xy; // 2913: and
        r9.xy = (asint(r11.xy) != asint(r10.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 2920: ine
        r5.w = r9.y  | r9.x; // 2927: or
        if (r5.w != 0u) { // 2934: if
          r10.zw = uint2(0x00000000u, 0x00000000u); // 2937: mov
          r10.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r10.xyw)).xyzw); // 2945: ld
          r7.xz = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r10.xy))); // 2954: mad
          r9.zw = asuint(mad(asfloat(r7.yy), asfloat(r10.zw), asfloat(r7.xz))); // 2964: mad
        } // 2973: endif
        r5.w = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.zw), asfloat(r8.w))); // 2974: sample_c_lz
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.zw), asfloat(r8.w), int2(-1, 0))); // 2987: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.x)); // 3001: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.zw), asfloat(r8.w), int2(1, 0))); // 3008: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.x)); // 3022: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.zw), asfloat(r8.w), int2(0, -1))); // 3029: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.x)); // 3043: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.zw), asfloat(r8.w), int2(0, 1))); // 3050: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.x)); // 3064: add
        r5.w = asuint(asfloat(r5.w) * 0.200000003f); // 3071: mul
      } else { // 3078: else
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xy), asfloat(r8.w))); // 3079: sample_c_lz
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xy), asfloat(r8.w), int2(-1, 0))); // 3092: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 3106: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xy), asfloat(r8.w), int2(1, 0))); // 3113: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 3127: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xy), asfloat(r8.w), int2(0, -1))); // 3134: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 3148: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xy), asfloat(r8.w), int2(0, 1))); // 3155: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 3169: add
        r5.w = asuint(asfloat(r7.x) * 0.200000003f); // 3176: mul
      } // 3183: endif
      r7.x = asuint(asfloat(r1.y) * abs(g_vBakedShadowmapParams.x)); // 3184: mul
      r7.x = asuint(min(asfloat(r7.x), 1.0f)); // 3193: min
      r7.x = asuint(asfloat(r7.x) * asfloat(r7.x)); // 3200: mul
      r5.w = asuint(mad(asfloat(r7.x), asfloat(r7.x), asfloat(r5.w))); // 3207: mad
      r1.y = asuint(mad(asfloat(r1.y), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 3216: mad
      r1.y = asuint(saturate(mad(asfloat(r1.y), 2.0f, -1.0f))); // 3227: mad
      r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 3236: add
      r1.y = asuint(min(asfloat(r1.y), 1.0f)); // 3243: min
      r8.z = asuint(min(asfloat(r1.y), asfloat(r8.z))); // 3250: min
    } // 3257: endif
    r7.xyz = asuint(mad(-(asfloat(r6.xxx)), asfloat(r4.xyz), v6.xyz)); // 3258: mad
    r7.xyz = asuint(mad(float3(view._m00, view._m10, view._m20), float3(0.0143827796f, 0.0143827796f, 0.0143827796f), asfloat(r7.xyz))); // 3268: mad
    r7.xyz = asuint(mad(float3(view._m01, view._m11, view._m21), float3(0.0263274908f, 0.0263274908f, 0.0263274908f), asfloat(r7.xyz))); // 3281: mad
    r7.w = 0x3f800000u; // 3294: mov
    r1.y = asuint(dot(asfloat(r7.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 3299: dp4
    r7.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), asfloat(r7.xyz))); // 3307: mad
    r5.w = (asfloat(r1.y) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 3319: lt
    if (r5.w != 0u) { // 3327: if
      r9.x = asuint(g_split_distances[1]); // 3330: mov
      r9.y = asuint(g_split_distances[2]); // 3336: mov
      r9.z = asuint(g_split_distances[3]); // 3342: mov
      r9.w = asuint(g_split_distances[4]); // 3348: mov
      r9.xyzw = (asfloat(r1.yyyy) >= asfloat(r9.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 3354: ge
      r9.xyzw = r9.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 3361: and
      r5.w = asuint(dot(asfloat(r9.xyzw), asfloat(r9.xyzw))); // 3371: dp4
      r5.w = asuint(asfloat(r5.w) + 0.5f); // 3378: add
      r5.w = asuint(floor(asfloat(r5.w))); // 3385: round_ni
      r5.w = asuint(min(asfloat(r4.w), asfloat(r5.w))); // 3390: min
      r6.x = asuint((int)(asfloat(r5.w))); // 3397: ftoi
      r8.x = r6.x << (0x00000002u & 31u); // 3402: ishl
      r7.w = 0x3f800000u; // 3409: mov
      r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m00, g_amHardSplit[(r8.x / 4u)]._m10, g_amHardSplit[(r8.x / 4u)]._m20, g_amHardSplit[(r8.x / 4u)]._m30))); // 3414: dp4
      r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m01, g_amHardSplit[(r8.x / 4u)]._m11, g_amHardSplit[(r8.x / 4u)]._m21, g_amHardSplit[(r8.x / 4u)]._m31))); // 3424: dp4
      r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m02, g_amHardSplit[(r8.x / 4u)]._m12, g_amHardSplit[(r8.x / 4u)]._m22, g_amHardSplit[(r8.x / 4u)]._m32))); // 3434: dp4
      r8.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m03, g_amHardSplit[(r8.x / 4u)]._m13, g_amHardSplit[(r8.x / 4u)]._m23, g_amHardSplit[(r8.x / 4u)]._m33))); // 3444: dp4
      r8.y = asuint(1.0f / asfloat(r8.y)); // 3454: div
      r10.xyz = asuint(asfloat(r8.yyy) * asfloat(r9.xyz)); // 3464: mul
      r8.w = asuint(g_iSplitCount * 0.5f); // 3471: mul
      r11.y = asuint(ceil(asfloat(r8.w))); // 3479: round_pi
      r11.x = 0x40000000u; // 3484: mov
      r8.w = r6.x  & 0x00000001u; // 3489: and
      r9.z = asuint(asint(r6.x) >> (0x00000001u & 31u)); // 3496: ishr
      r9.z = r9.z  & 0x00000001u; // 3503: and
      r12.x = asuint((float)(asint(r8.w))); // 3510: itof
      r12.y = asuint((float)(asint(r9.z))); // 3515: itof
      r9.zw = asuint(mad(asfloat(r10.xy), asfloat(r11.xy), -(asfloat(r12.xy)))); // 3520: mad
      r9.zw = asuint(mad(asfloat(r9.zw), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 3530: mad
      r10.xy = asuint(saturate(asfloat(r9.zw))); // 3545: mov
      r9.zw = (asfloat(r9.zw) == asfloat(r10.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 3550: eq
      r5.w = asuint(trunc(asfloat(r5.w))); // 3557: round_z
      r5.w = (asfloat(r4.w) == asfloat(r5.w)) ? 0xffffffffu : 0u; // 3562: eq
      r8.w = r9.w  & r9.z; // 3569: and
      r9.z = (asint(r5.w) == -1) ? 0xffffffffu : 0u; // 3576: ieq
      r8.w = r8.w  | r9.z; // 3583: or
      if (r8.w != 0u) { // 3590: if
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 3593: mul
        r12.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r11.xyzw))); // 3604: mad
        r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r10.z))); // 3613: sample_c
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r10.z))); // 3626: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3639: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 3646: mul
        r13.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r12.xyzw))); // 3657: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r10.z))); // 3666: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3679: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r10.z))); // 3686: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3699: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 3706: mul
        r14.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r13.xyzw))); // 3717: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r10.z))); // 3726: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3739: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r10.z))); // 3746: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3759: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 3766: mul
        r15.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r14.xyzw))); // 3777: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r10.z))); // 3786: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3799: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r10.z))); // 3806: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3819: add
        r15.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 3826: mul
        r16.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r15.xyzw))); // 3837: mad
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.xy), asfloat(r10.z))); // 3846: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3859: add
        r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.zw), asfloat(r10.z))); // 3866: sample_c
        r8.w = asuint(asfloat(r8.w) + asfloat(r9.w)); // 3879: add
        r16.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 3886: mul
        r17.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r16.xyzw))); // 3897: mad
        r8.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r17.xy), asfloat(r10.z))); // 3906: sample_c
        r8.y = asuint(asfloat(r8.y) + asfloat(r8.w)); // 3919: add
        r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r17.zw), asfloat(r10.z))); // 3926: sample_c
        r8.y = asuint(asfloat(r8.w) + asfloat(r8.y)); // 3939: add
        r8.y = asuint(asfloat(r8.y) * 0.0833333358f); // 3946: mul
        if (r5.w == 0u) { // 3953: if
          r5.w = asuint(asint(r8.x) + 4); // 3956: iadd
          r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m00, g_amHardSplit[(r5.w / 4u)]._m10, g_amHardSplit[(r5.w / 4u)]._m20, g_amHardSplit[(r5.w / 4u)]._m30))); // 3963: dp4
          r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m01, g_amHardSplit[(r5.w / 4u)]._m11, g_amHardSplit[(r5.w / 4u)]._m21, g_amHardSplit[(r5.w / 4u)]._m31))); // 3973: dp4
          r8.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m02, g_amHardSplit[(r5.w / 4u)]._m12, g_amHardSplit[(r5.w / 4u)]._m22, g_amHardSplit[(r5.w / 4u)]._m32))); // 3983: dp4
          r5.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m03, g_amHardSplit[(r5.w / 4u)]._m13, g_amHardSplit[(r5.w / 4u)]._m23, g_amHardSplit[(r5.w / 4u)]._m33))); // 3993: dp4
          r5.w = asuint(1.0f / asfloat(r5.w)); // 4003: div
          r8.w = asuint(asfloat(r5.w) * asfloat(r8.w)); // 4013: mul
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r11.xyzw))); // 4020: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4029: sample_c
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4042: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4055: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r12.xyzw))); // 4062: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4071: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4084: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4091: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4104: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r13.xyzw))); // 4111: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4120: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4133: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4140: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4153: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r14.xyzw))); // 4160: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4169: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4182: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4189: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4202: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r15.xyzw))); // 4209: mad
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4218: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4231: add
          r10.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4238: sample_c
          r9.w = asuint(asfloat(r9.w) + asfloat(r10.x)); // 4251: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r5.wwww), asfloat(r16.xyzw))); // 4258: mad
          r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.w))); // 4267: sample_c
          r5.w = asuint(asfloat(r5.w) + asfloat(r9.w)); // 4280: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.w))); // 4287: sample_c
          r5.w = asuint(asfloat(r5.w) + asfloat(r8.w)); // 4300: add
          r8.w = asuint(asint(r6.x) + 1); // 4307: iadd
          r9.x = asuint(-(g_split_distances[r6.x]) + g_split_distances[r8.w]); // 4314: add
          r9.y = asuint(asfloat(r9.x) * g_fFadeRange.x); // 4328: mul
          r8.w = asuint(mad(-(asfloat(r9.x)), g_fFadeRange.x, g_split_distances[r8.w])); // 4336: mad
          r8.w = asuint(asfloat(r1.y) + -(asfloat(r8.w))); // 4350: add
          r8.w = asuint(saturate(asfloat(r8.w) / asfloat(r9.y))); // 4358: div
          r5.w = asuint(mad(asfloat(r5.w), 0.0833333358f, -(asfloat(r8.y)))); // 4365: mad
          r8.y = asuint(mad(asfloat(r8.w), asfloat(r5.w), asfloat(r8.y))); // 4375: mad
        } // 4384: endif
        if (r9.z != 0u) { // 4385: if
          r5.w = asuint(asint(r6.x) + 1); // 4388: iadd
          r5.w = asuint(-(g_split_distances[r6.x]) + g_split_distances[r5.w]); // 4395: add
          r8.w = asuint(-(g_fFadeRange.y) + 1.0f); // 4409: add
          r6.x = asuint(mad(asfloat(r5.w), asfloat(r8.w), g_split_distances[r6.x])); // 4418: mad
          r1.y = asuint(asfloat(r1.y) + -(asfloat(r6.x))); // 4430: add
          r5.w = asuint(asfloat(r5.w) * g_fFadeRange.y); // 4438: mul
          r1.y = asuint(saturate(asfloat(r1.y) / asfloat(r5.w))); // 4446: div
          r5.w = asuint(-(asfloat(r8.y)) + 1.0f); // 4453: add
          r8.y = asuint(mad(asfloat(r1.y), asfloat(r5.w), asfloat(r8.y))); // 4461: mad
        } // 4470: endif
      } else { // 4471: else
        r1.y = asuint(asint(r8.x) + 4); // 4472: iadd
        r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m00, g_amHardSplit[(r1.y / 4u)]._m10, g_amHardSplit[(r1.y / 4u)]._m20, g_amHardSplit[(r1.y / 4u)]._m30))); // 4479: dp4
        r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m01, g_amHardSplit[(r1.y / 4u)]._m11, g_amHardSplit[(r1.y / 4u)]._m21, g_amHardSplit[(r1.y / 4u)]._m31))); // 4489: dp4
        r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m02, g_amHardSplit[(r1.y / 4u)]._m12, g_amHardSplit[(r1.y / 4u)]._m22, g_amHardSplit[(r1.y / 4u)]._m32))); // 4499: dp4
        r1.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r1.y / 4u)]._m03, g_amHardSplit[(r1.y / 4u)]._m13, g_amHardSplit[(r1.y / 4u)]._m23, g_amHardSplit[(r1.y / 4u)]._m33))); // 4509: dp4
        r1.y = asuint(1.0f / asfloat(r1.y)); // 4519: div
        r9.xyz = asuint(asfloat(r1.yyy) * asfloat(r9.xyz)); // 4529: mul
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r9.xyxy))); // 4536: mad
        r1.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4549: sample_c
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4562: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4575: add
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r9.xyxy))); // 4582: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4595: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4608: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4615: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4628: add
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r9.xyxy))); // 4635: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4648: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4661: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4668: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4681: add
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r9.xyxy))); // 4688: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4701: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4714: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4721: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4734: add
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r9.xyxy))); // 4741: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4754: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4767: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4774: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4787: add
        r10.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r9.xyxy))); // 4794: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r9.z))); // 4807: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4820: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r9.z))); // 4827: sample_c
        r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 4840: add
        r8.y = asuint(asfloat(r1.y) * 0.0833333358f); // 4847: mul
      } // 4854: endif
    } else { // 4855: else
      r8.y = 0x3f800000u; // 4856: mov
    } // 4861: endif
    r9.xyz = asuint(asfloat(r7.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 4862: add
    r1.y = asuint(dot(asfloat(r9.xyz), asfloat(r9.xyz))); // 4871: dp3
    r1.y = asuint(sqrt(asfloat(r1.y))); // 4878: sqrt
    r5.w = (asfloat(r1.y) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 4883: ge
    if (r5.w != 0u) { // 4891: if
      r9.xyz = asuint(mad(asfloat(r7.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 4894: mad
      r8.xw = asuint(mad(asfloat(r7.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r9.xy))); // 4905: mad
      r5.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 4915: lt
      if (r5.w != 0u) { // 4923: if
        uint4 dimensions_4926 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_4926.x, dimensions_4926.y, dimensions_4926.w);
        r9.xy = asuint((float2)(dimensions_4926.xy)); // 4926: resinfo
        r10.xy = asuint(asfloat(r8.xw) * asfloat(r9.xy)); // 4935: mul
        r10.xy = asuint(floor(asfloat(r10.xy))); // 4942: round_ni
        r10.xy = asuint((int2)(asfloat(r10.xy))); // 4947: ftoi
        r10.zw = asuint((int2)(asfloat(r9.xy))); // 4952: ftoi
        r10.zw = asuint(asint(r10.zw) + int2(-1, -1)); // 4957: iadd
        r11.xy = r10.zw  & r10.xy; // 4967: and
        r11.zw = uint2(0x00000000u, 0x00000000u); // 4974: mov
        r12.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r11.xyw)).xyzw); // 4982: ld
        r10.xy = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r12.xy))); // 4991: mad
        r10.xy = asuint(mad(asfloat(r7.yy), asfloat(r12.zw), asfloat(r10.xy))); // 5001: mad
        r9.xy = asuint(asfloat(r9.xy) * asfloat(r10.xy)); // 5010: mul
        r9.xy = asuint(floor(asfloat(r9.xy))); // 5017: round_ni
        r9.xy = asuint((int2)(asfloat(r9.xy))); // 5022: ftoi
        r12.xy = r10.zw  & r9.xy; // 5027: and
        r9.xy = (asint(r11.xy) != asint(r12.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 5034: ine
        r5.w = r9.y  | r9.x; // 5041: or
        if (r5.w != 0u) { // 5048: if
          r12.zw = uint2(0x00000000u, 0x00000000u); // 5051: mov
          r11.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r12.xyw)).xyzw); // 5059: ld
          r7.xz = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r11.xy))); // 5068: mad
          r10.xy = asuint(mad(asfloat(r7.yy), asfloat(r11.zw), asfloat(r7.xz))); // 5078: mad
        } // 5087: endif
        r5.w = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r10.xy), asfloat(r9.z))); // 5088: sample_c_lz
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r10.xy), asfloat(r9.z), int2(-1, 0))); // 5101: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 5115: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r10.xy), asfloat(r9.z), int2(1, 0))); // 5122: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 5136: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r10.xy), asfloat(r9.z), int2(0, -1))); // 5143: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 5157: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r10.xy), asfloat(r9.z), int2(0, 1))); // 5164: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 5178: add
        r5.w = asuint(asfloat(r5.w) * 0.200000003f); // 5185: mul
      } else { // 5192: else
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xw), asfloat(r9.z))); // 5193: sample_c_lz
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xw), asfloat(r9.z), int2(-1, 0))); // 5206: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.x)); // 5220: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xw), asfloat(r9.z), int2(1, 0))); // 5227: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.x)); // 5241: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xw), asfloat(r9.z), int2(0, -1))); // 5248: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.x)); // 5262: add
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.xw), asfloat(r9.z), int2(0, 1))); // 5269: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.x)); // 5283: add
        r5.w = asuint(asfloat(r6.x) * 0.200000003f); // 5290: mul
      } // 5297: endif
      r6.x = asuint(asfloat(r1.y) * abs(g_vBakedShadowmapParams.x)); // 5298: mul
      r6.x = asuint(min(asfloat(r6.x), 1.0f)); // 5307: min
      r6.x = asuint(asfloat(r6.x) * asfloat(r6.x)); // 5314: mul
      r5.w = asuint(mad(asfloat(r6.x), asfloat(r6.x), asfloat(r5.w))); // 5321: mad
      r1.y = asuint(mad(asfloat(r1.y), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 5330: mad
      r1.y = asuint(saturate(mad(asfloat(r1.y), 2.0f, -1.0f))); // 5341: mad
      r1.y = asuint(asfloat(r1.y) + asfloat(r5.w)); // 5350: add
      r1.y = asuint(min(asfloat(r1.y), 1.0f)); // 5357: min
      r8.y = asuint(min(asfloat(r1.y), asfloat(r8.y))); // 5364: min
    } // 5371: endif
    r8.yz = asuint(saturate(asfloat(r8.yz))); // 5372: mov
    r1.y = asuint(asfloat(r8.y) + asfloat(r8.z)); // 5377: add
    r7.xyz = asuint(mad(-(asfloat(r6.yyy)), asfloat(r4.xyz), v6.xyz)); // 5384: mad
    r7.xyz = asuint(mad(float3(view._m00, view._m10, view._m20), float3(0.0504882f, 0.0504882f, 0.0504882f), asfloat(r7.xyz))); // 5394: mad
    r7.xyz = asuint(mad(float3(view._m01, view._m11, view._m21), float3(0.0324180014f, 0.0324180014f, 0.0324180014f), asfloat(r7.xyz))); // 5407: mad
    r7.w = 0x3f800000u; // 5420: mov
    r5.w = asuint(dot(asfloat(r7.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 5425: dp4
    r7.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), asfloat(r7.xyz))); // 5433: mad
    r6.x = (asfloat(r5.w) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 5445: lt
    if (r6.x != 0u) { // 5453: if
      r8.x = asuint(g_split_distances[1]); // 5456: mov
      r8.y = asuint(g_split_distances[2]); // 5462: mov
      r8.z = asuint(g_split_distances[3]); // 5468: mov
      r8.w = asuint(g_split_distances[4]); // 5474: mov
      r8.xyzw = (asfloat(r5.wwww) >= asfloat(r8.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 5480: ge
      r8.xyzw = r8.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 5487: and
      r6.x = asuint(dot(asfloat(r8.xyzw), asfloat(r8.xyzw))); // 5497: dp4
      r6.x = asuint(asfloat(r6.x) + 0.5f); // 5504: add
      r6.x = asuint(floor(asfloat(r6.x))); // 5511: round_ni
      r6.x = asuint(min(asfloat(r4.w), asfloat(r6.x))); // 5516: min
      r6.y = asuint((int)(asfloat(r6.x))); // 5523: ftoi
      r8.x = r6.y << (0x00000002u & 31u); // 5528: ishl
      r7.w = 0x3f800000u; // 5535: mov
      r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m00, g_amHardSplit[(r8.x / 4u)]._m10, g_amHardSplit[(r8.x / 4u)]._m20, g_amHardSplit[(r8.x / 4u)]._m30))); // 5540: dp4
      r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m01, g_amHardSplit[(r8.x / 4u)]._m11, g_amHardSplit[(r8.x / 4u)]._m21, g_amHardSplit[(r8.x / 4u)]._m31))); // 5550: dp4
      r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m02, g_amHardSplit[(r8.x / 4u)]._m12, g_amHardSplit[(r8.x / 4u)]._m22, g_amHardSplit[(r8.x / 4u)]._m32))); // 5560: dp4
      r8.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r8.x / 4u)]._m03, g_amHardSplit[(r8.x / 4u)]._m13, g_amHardSplit[(r8.x / 4u)]._m23, g_amHardSplit[(r8.x / 4u)]._m33))); // 5570: dp4
      r8.y = asuint(1.0f / asfloat(r8.y)); // 5580: div
      r10.xyz = asuint(asfloat(r8.yyy) * asfloat(r9.xyz)); // 5590: mul
      r8.z = asuint(g_iSplitCount * 0.5f); // 5597: mul
      r11.y = asuint(ceil(asfloat(r8.z))); // 5605: round_pi
      r11.x = 0x40000000u; // 5610: mov
      r8.z = r6.y  & 0x00000001u; // 5615: and
      r8.w = asuint(asint(r6.y) >> (0x00000001u & 31u)); // 5622: ishr
      r8.w = r8.w  & 0x00000001u; // 5629: and
      r12.xy = asuint((float2)(asint(r8.zw))); // 5636: itof
      r8.zw = asuint(mad(asfloat(r10.xy), asfloat(r11.xy), -(asfloat(r12.xy)))); // 5641: mad
      r8.zw = asuint(mad(asfloat(r8.zw), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 5651: mad
      r9.zw = asuint(saturate(asfloat(r8.zw))); // 5666: mov
      r8.zw = (asfloat(r8.zw) == asfloat(r9.zw)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 5671: eq
      r6.x = asuint(trunc(asfloat(r6.x))); // 5678: round_z
      r6.x = (asfloat(r4.w) == asfloat(r6.x)) ? 0xffffffffu : 0u; // 5683: eq
      r8.z = r8.w  & r8.z; // 5690: and
      r8.w = (asint(r6.x) == -1) ? 0xffffffffu : 0u; // 5697: ieq
      r8.z = r8.w  | r8.z; // 5704: or
      if (r8.z != 0u) { // 5711: if
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 5714: mul
        r12.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r11.xyzw))); // 5725: mad
        r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r10.z))); // 5734: sample_c
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r10.z))); // 5747: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5760: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 5767: mul
        r13.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r12.xyzw))); // 5778: mad
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r10.z))); // 5787: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5800: add
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r10.z))); // 5807: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5820: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 5827: mul
        r14.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r13.xyzw))); // 5838: mad
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r10.z))); // 5847: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5860: add
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r10.z))); // 5867: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5880: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 5887: mul
        r15.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r14.xyzw))); // 5898: mad
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r10.z))); // 5907: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5920: add
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r10.z))); // 5927: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5940: add
        r15.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 5947: mul
        r16.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r15.xyzw))); // 5958: mad
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.xy), asfloat(r10.z))); // 5967: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 5980: add
        r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.zw), asfloat(r10.z))); // 5987: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.z)); // 6000: add
        r16.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 6007: mul
        r9.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r8.yyyy), asfloat(r16.xyzw))); // 6018: mad
        r8.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r10.z))); // 6027: sample_c
        r8.y = asuint(asfloat(r8.y) + asfloat(r8.z)); // 6040: add
        r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r10.z))); // 6047: sample_c
        r8.y = asuint(asfloat(r8.z) + asfloat(r8.y)); // 6060: add
        r8.y = asuint(asfloat(r8.y) * 0.0833333358f); // 6067: mul
        if (r6.x == 0u) { // 6074: if
          r6.x = asuint(asint(r8.x) + 4); // 6077: iadd
          r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m00, g_amHardSplit[(r6.x / 4u)]._m10, g_amHardSplit[(r6.x / 4u)]._m20, g_amHardSplit[(r6.x / 4u)]._m30))); // 6084: dp4
          r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m01, g_amHardSplit[(r6.x / 4u)]._m11, g_amHardSplit[(r6.x / 4u)]._m21, g_amHardSplit[(r6.x / 4u)]._m31))); // 6094: dp4
          r8.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m02, g_amHardSplit[(r6.x / 4u)]._m12, g_amHardSplit[(r6.x / 4u)]._m22, g_amHardSplit[(r6.x / 4u)]._m32))); // 6104: dp4
          r6.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m03, g_amHardSplit[(r6.x / 4u)]._m13, g_amHardSplit[(r6.x / 4u)]._m23, g_amHardSplit[(r6.x / 4u)]._m33))); // 6114: dp4
          r6.x = asuint(1.0f / asfloat(r6.x)); // 6124: div
          r8.z = asuint(asfloat(r6.x) * asfloat(r8.z)); // 6134: mul
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r11.xyzw))); // 6141: mad
          r9.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6150: sample_c
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6163: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6176: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r12.xyzw))); // 6183: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6192: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6205: add
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6212: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6225: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r13.xyzw))); // 6232: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6241: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6254: add
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6261: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6274: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r14.xyzw))); // 6281: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6290: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6303: add
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6310: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6323: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r15.xyzw))); // 6330: mad
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6339: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6352: add
          r9.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6359: sample_c
          r9.z = asuint(asfloat(r9.w) + asfloat(r9.z)); // 6372: add
          r10.xyzw = asuint(mad(asfloat(r9.xyxy), asfloat(r6.xxxx), asfloat(r16.xyzw))); // 6379: mad
          r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 6388: sample_c
          r6.x = asuint(asfloat(r6.x) + asfloat(r9.z)); // 6401: add
          r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 6408: sample_c
          r6.x = asuint(asfloat(r6.x) + asfloat(r8.z)); // 6421: add
          r8.z = asuint(asint(r6.y) + 1); // 6428: iadd
          r9.x = asuint(-(g_split_distances[r6.y]) + g_split_distances[r8.z]); // 6435: add
          r9.y = asuint(asfloat(r9.x) * g_fFadeRange.x); // 6449: mul
          r8.z = asuint(mad(-(asfloat(r9.x)), g_fFadeRange.x, g_split_distances[r8.z])); // 6457: mad
          r8.z = asuint(asfloat(r5.w) + -(asfloat(r8.z))); // 6471: add
          r8.z = asuint(saturate(asfloat(r8.z) / asfloat(r9.y))); // 6479: div
          r6.x = asuint(mad(asfloat(r6.x), 0.0833333358f, -(asfloat(r8.y)))); // 6486: mad
          r8.y = asuint(mad(asfloat(r8.z), asfloat(r6.x), asfloat(r8.y))); // 6496: mad
        } // 6505: endif
        if (r8.w != 0u) { // 6506: if
          r6.x = asuint(asint(r6.y) + 1); // 6509: iadd
          r6.x = asuint(-(g_split_distances[r6.y]) + g_split_distances[r6.x]); // 6516: add
          r8.z = asuint(-(g_fFadeRange.y) + 1.0f); // 6530: add
          r6.y = asuint(mad(asfloat(r6.x), asfloat(r8.z), g_split_distances[r6.y])); // 6539: mad
          r5.w = asuint(asfloat(r5.w) + -(asfloat(r6.y))); // 6551: add
          r6.x = asuint(asfloat(r6.x) * g_fFadeRange.y); // 6559: mul
          r5.w = asuint(saturate(asfloat(r5.w) / asfloat(r6.x))); // 6567: div
          r6.x = asuint(-(asfloat(r8.y)) + 1.0f); // 6574: add
          r8.y = asuint(mad(asfloat(r5.w), asfloat(r6.x), asfloat(r8.y))); // 6582: mad
        } // 6591: endif
      } else { // 6592: else
        r5.w = asuint(asint(r8.x) + 4); // 6593: iadd
        r9.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m00, g_amHardSplit[(r5.w / 4u)]._m10, g_amHardSplit[(r5.w / 4u)]._m20, g_amHardSplit[(r5.w / 4u)]._m30))); // 6600: dp4
        r9.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m01, g_amHardSplit[(r5.w / 4u)]._m11, g_amHardSplit[(r5.w / 4u)]._m21, g_amHardSplit[(r5.w / 4u)]._m31))); // 6610: dp4
        r9.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m02, g_amHardSplit[(r5.w / 4u)]._m12, g_amHardSplit[(r5.w / 4u)]._m22, g_amHardSplit[(r5.w / 4u)]._m32))); // 6620: dp4
        r5.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m03, g_amHardSplit[(r5.w / 4u)]._m13, g_amHardSplit[(r5.w / 4u)]._m23, g_amHardSplit[(r5.w / 4u)]._m33))); // 6630: dp4
        r5.w = asuint(1.0f / asfloat(r5.w)); // 6640: div
        r8.xzw = asuint(asfloat(r5.www) * asfloat(r9.xyz)); // 6650: mul
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r8.xzxz))); // 6657: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6670: sample_c
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6683: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6696: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r8.xzxz))); // 6703: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6716: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6729: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6736: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6749: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r8.xzxz))); // 6756: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6769: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6782: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6789: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6802: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r8.xzxz))); // 6809: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6822: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6835: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6842: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6855: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r8.xzxz))); // 6862: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6875: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6888: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6895: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6908: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r8.xzxz))); // 6915: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.w))); // 6928: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6941: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.w))); // 6948: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 6961: add
        r8.y = asuint(asfloat(r5.w) * 0.0833333358f); // 6968: mul
      } // 6975: endif
    } else { // 6976: else
      r8.y = 0x3f800000u; // 6977: mov
    } // 6982: endif
    r8.xzw = asuint(asfloat(r7.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 6983: add
    r5.w = asuint(dot(asfloat(r8.xzw), asfloat(r8.xzw))); // 6992: dp3
    r5.w = asuint(sqrt(asfloat(r5.w))); // 6999: sqrt
    r6.x = (asfloat(r5.w) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 7004: ge
    if (r6.x != 0u) { // 7012: if
      r8.xzw = asuint(mad(asfloat(r7.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 7015: mad
      r6.xy = asuint(mad(asfloat(r7.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r8.xz))); // 7026: mad
      r7.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 7036: lt
      if (r7.w != 0u) { // 7044: if
        uint4 dimensions_7047 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_7047.x, dimensions_7047.y, dimensions_7047.w);
        r8.xz = asuint((float2)(dimensions_7047.xy)); // 7047: resinfo
        r9.xy = asuint(asfloat(r6.xy) * asfloat(r8.xz)); // 7056: mul
        r9.xy = asuint(floor(asfloat(r9.xy))); // 7063: round_ni
        r9.xy = asuint((int2)(asfloat(r9.xy))); // 7068: ftoi
        r9.zw = asuint((int2)(asfloat(r8.xz))); // 7073: ftoi
        r9.zw = asuint(asint(r9.zw) + int2(-1, -1)); // 7078: iadd
        r10.xy = r9.zw  & r9.xy; // 7088: and
        r10.zw = uint2(0x00000000u, 0x00000000u); // 7095: mov
        r11.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r10.xyw)).xyzw); // 7103: ld
        r9.xy = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r11.xy))); // 7112: mad
        r9.xy = asuint(mad(asfloat(r7.yy), asfloat(r11.zw), asfloat(r9.xy))); // 7122: mad
        r8.xz = asuint(asfloat(r8.xz) * asfloat(r9.xy)); // 7131: mul
        r8.xz = asuint(floor(asfloat(r8.xz))); // 7138: round_ni
        r8.xz = asuint((int2)(asfloat(r8.xz))); // 7143: ftoi
        r11.xy = r9.zw  & r8.xz; // 7148: and
        r8.xz = (asint(r10.xy) != asint(r11.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 7155: ine
        r7.w = r8.z  | r8.x; // 7162: or
        if (r7.w != 0u) { // 7169: if
          r11.zw = uint2(0x00000000u, 0x00000000u); // 7172: mov
          r10.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r11.xyw)).xyzw); // 7180: ld
          r7.xz = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r10.xy))); // 7189: mad
          r9.xy = asuint(mad(asfloat(r7.yy), asfloat(r10.zw), asfloat(r7.xz))); // 7199: mad
        } // 7208: endif
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r8.w))); // 7209: sample_c_lz
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r8.w), int2(-1, 0))); // 7222: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 7236: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r8.w), int2(1, 0))); // 7243: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 7257: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r8.w), int2(0, -1))); // 7264: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 7278: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r8.w), int2(0, 1))); // 7285: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 7299: add
        r7.x = asuint(asfloat(r7.x) * 0.200000003f); // 7306: mul
      } else { // 7313: else
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r8.w))); // 7314: sample_c_lz
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r8.w), int2(-1, 0))); // 7327: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 7341: add
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r8.w), int2(1, 0))); // 7348: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 7362: add
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r8.w), int2(0, -1))); // 7369: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 7383: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r8.w), int2(0, 1))); // 7390: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.y)); // 7404: add
        r7.x = asuint(asfloat(r6.x) * 0.200000003f); // 7411: mul
      } // 7418: endif
      r6.x = asuint(asfloat(r5.w) * abs(g_vBakedShadowmapParams.x)); // 7419: mul
      r6.x = asuint(min(asfloat(r6.x), 1.0f)); // 7428: min
      r6.x = asuint(asfloat(r6.x) * asfloat(r6.x)); // 7435: mul
      r6.x = asuint(mad(asfloat(r6.x), asfloat(r6.x), asfloat(r7.x))); // 7442: mad
      r5.w = asuint(mad(asfloat(r5.w), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 7451: mad
      r5.w = asuint(saturate(mad(asfloat(r5.w), 2.0f, -1.0f))); // 7462: mad
      r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 7471: add
      r5.w = asuint(min(asfloat(r5.w), 1.0f)); // 7478: min
      r8.y = asuint(min(asfloat(r5.w), asfloat(r8.y))); // 7485: min
    } // 7492: endif
    r8.y = asuint(saturate(asfloat(r8.y))); // 7493: mov
    r1.y = asuint(asfloat(r1.y) + asfloat(r8.y)); // 7498: add
    r6.xyz = asuint(mad(-(asfloat(r6.zzz)), asfloat(r4.xyz), v6.xyz)); // 7505: mad
    r6.xyz = asuint(mad(float3(view._m00, view._m10, view._m20), float3(0.089774698f, 0.089774698f, 0.089774698f), asfloat(r6.xyz))); // 7515: mad
    r7.xyz = asuint(mad(float3(view._m01, view._m11, view._m21), float3(0.00636635954f, 0.00636635954f, 0.00636635954f), asfloat(r6.xyz))); // 7528: mad
    r7.w = 0x3f800000u; // 7541: mov
    r5.w = asuint(dot(asfloat(r7.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 7546: dp4
    r7.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), asfloat(r7.xyz))); // 7554: mad
    r6.x = (asfloat(r5.w) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 7566: lt
    if (r6.x != 0u) { // 7574: if
      r8.x = asuint(g_split_distances[1]); // 7577: mov
      r8.y = asuint(g_split_distances[2]); // 7583: mov
      r8.z = asuint(g_split_distances[3]); // 7589: mov
      r8.w = asuint(g_split_distances[4]); // 7595: mov
      r8.xyzw = (asfloat(r5.wwww) >= asfloat(r8.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 7601: ge
      r8.xyzw = r8.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 7608: and
      r6.x = asuint(dot(asfloat(r8.xyzw), asfloat(r8.xyzw))); // 7618: dp4
      r6.x = asuint(asfloat(r6.x) + 0.5f); // 7625: add
      r6.x = asuint(floor(asfloat(r6.x))); // 7632: round_ni
      r6.x = asuint(min(asfloat(r4.w), asfloat(r6.x))); // 7637: min
      r6.y = asuint((int)(asfloat(r6.x))); // 7644: ftoi
      r6.z = r6.y << (0x00000002u & 31u); // 7649: ishl
      r7.w = 0x3f800000u; // 7656: mov
      r8.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.z / 4u)]._m00, g_amHardSplit[(r6.z / 4u)]._m10, g_amHardSplit[(r6.z / 4u)]._m20, g_amHardSplit[(r6.z / 4u)]._m30))); // 7661: dp4
      r8.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.z / 4u)]._m01, g_amHardSplit[(r6.z / 4u)]._m11, g_amHardSplit[(r6.z / 4u)]._m21, g_amHardSplit[(r6.z / 4u)]._m31))); // 7671: dp4
      r8.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.z / 4u)]._m02, g_amHardSplit[(r6.z / 4u)]._m12, g_amHardSplit[(r6.z / 4u)]._m22, g_amHardSplit[(r6.z / 4u)]._m32))); // 7681: dp4
      r8.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.z / 4u)]._m03, g_amHardSplit[(r6.z / 4u)]._m13, g_amHardSplit[(r6.z / 4u)]._m23, g_amHardSplit[(r6.z / 4u)]._m33))); // 7691: dp4
      r8.w = asuint(1.0f / asfloat(r8.w)); // 7701: div
      r9.xyz = asuint(asfloat(r8.www) * asfloat(r8.xyz)); // 7711: mul
      r8.z = asuint(g_iSplitCount * 0.5f); // 7718: mul
      r10.y = asuint(ceil(asfloat(r8.z))); // 7726: round_pi
      r10.x = 0x40000000u; // 7731: mov
      r8.z = r6.y  & 0x00000001u; // 7736: and
      r9.w = asuint(asint(r6.y) >> (0x00000001u & 31u)); // 7743: ishr
      r9.w = r9.w  & 0x00000001u; // 7750: and
      r11.x = asuint((float)(asint(r8.z))); // 7757: itof
      r11.y = asuint((float)(asint(r9.w))); // 7762: itof
      r9.xy = asuint(mad(asfloat(r9.xy), asfloat(r10.xy), -(asfloat(r11.xy)))); // 7767: mad
      r9.xy = asuint(mad(asfloat(r9.xy), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 7777: mad
      r10.xy = asuint(saturate(asfloat(r9.xy))); // 7792: mov
      r9.xy = (asfloat(r9.xy) == asfloat(r10.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 7797: eq
      r6.x = asuint(trunc(asfloat(r6.x))); // 7804: round_z
      r6.x = (asfloat(r4.w) == asfloat(r6.x)) ? 0xffffffffu : 0u; // 7809: eq
      r8.z = r9.y  & r9.x; // 7816: and
      r9.x = (asint(r6.x) == -1) ? 0xffffffffu : 0u; // 7823: ieq
      r8.z = r8.z  | r9.x; // 7830: or
      if (r8.z != 0u) { // 7837: if
        r10.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 7840: mul
        r11.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r10.xyzw))); // 7851: mad
        r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.xy), asfloat(r9.z))); // 7860: sample_c
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.zw), asfloat(r9.z))); // 7873: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 7886: add
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 7893: mul
        r12.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r11.xyzw))); // 7904: mad
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r9.z))); // 7913: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 7926: add
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r9.z))); // 7933: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 7946: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 7953: mul
        r13.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r12.xyzw))); // 7964: mad
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r9.z))); // 7973: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 7986: add
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r9.z))); // 7993: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 8006: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 8013: mul
        r14.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r13.xyzw))); // 8024: mad
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r9.z))); // 8033: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 8046: add
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r9.z))); // 8053: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 8066: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 8073: mul
        r15.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r14.xyzw))); // 8084: mad
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r9.z))); // 8093: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 8106: add
        r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r9.z))); // 8113: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.y)); // 8126: add
        r15.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 8133: mul
        r16.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r8.wwww), asfloat(r15.xyzw))); // 8144: mad
        r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.xy), asfloat(r9.z))); // 8153: sample_c
        r8.x = asuint(asfloat(r8.x) + asfloat(r8.z)); // 8166: add
        r8.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.zw), asfloat(r9.z))); // 8173: sample_c
        r8.x = asuint(asfloat(r8.y) + asfloat(r8.x)); // 8186: add
        r8.x = asuint(asfloat(r8.x) * 0.0833333358f); // 8193: mul
        if (r6.x == 0u) { // 8200: if
          r6.x = asuint(asint(r6.z) + 4); // 8203: iadd
          r16.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m00, g_amHardSplit[(r6.x / 4u)]._m10, g_amHardSplit[(r6.x / 4u)]._m20, g_amHardSplit[(r6.x / 4u)]._m30))); // 8210: dp4
          r16.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m01, g_amHardSplit[(r6.x / 4u)]._m11, g_amHardSplit[(r6.x / 4u)]._m21, g_amHardSplit[(r6.x / 4u)]._m31))); // 8220: dp4
          r8.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m02, g_amHardSplit[(r6.x / 4u)]._m12, g_amHardSplit[(r6.x / 4u)]._m22, g_amHardSplit[(r6.x / 4u)]._m32))); // 8230: dp4
          r6.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r6.x / 4u)]._m03, g_amHardSplit[(r6.x / 4u)]._m13, g_amHardSplit[(r6.x / 4u)]._m23, g_amHardSplit[(r6.x / 4u)]._m33))); // 8240: dp4
          r6.x = asuint(1.0f / asfloat(r6.x)); // 8250: div
          r8.y = asuint(asfloat(r6.x) * asfloat(r8.y)); // 8260: mul
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r10.xyzw))); // 8267: mad
          r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8276: sample_c
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8289: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8302: add
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r11.xyzw))); // 8309: mad
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8318: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8331: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8338: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8351: add
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r12.xyzw))); // 8358: mad
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8367: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8380: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8387: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8400: add
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r13.xyzw))); // 8407: mad
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8416: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8429: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8436: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8449: add
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r14.xyzw))); // 8456: mad
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8465: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8478: add
          r8.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8485: sample_c
          r8.z = asuint(asfloat(r8.w) + asfloat(r8.z)); // 8498: add
          r10.xyzw = asuint(mad(asfloat(r16.xyxy), asfloat(r6.xxxx), asfloat(r15.xyzw))); // 8505: mad
          r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.y))); // 8514: sample_c
          r6.x = asuint(asfloat(r6.x) + asfloat(r8.z)); // 8527: add
          r8.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.y))); // 8534: sample_c
          r6.x = asuint(asfloat(r6.x) + asfloat(r8.y)); // 8547: add
          r8.y = asuint(asint(r6.y) + 1); // 8554: iadd
          r8.z = asuint(-(g_split_distances[r6.y]) + g_split_distances[r8.y]); // 8561: add
          r8.w = asuint(asfloat(r8.z) * g_fFadeRange.x); // 8575: mul
          r8.y = asuint(mad(-(asfloat(r8.z)), g_fFadeRange.x, g_split_distances[r8.y])); // 8583: mad
          r8.y = asuint(asfloat(r5.w) + -(asfloat(r8.y))); // 8597: add
          r8.y = asuint(saturate(asfloat(r8.y) / asfloat(r8.w))); // 8605: div
          r6.x = asuint(mad(asfloat(r6.x), 0.0833333358f, -(asfloat(r8.x)))); // 8612: mad
          r8.x = asuint(mad(asfloat(r8.y), asfloat(r6.x), asfloat(r8.x))); // 8622: mad
        } // 8631: endif
        if (r9.x != 0u) { // 8632: if
          r6.x = asuint(asint(r6.y) + 1); // 8635: iadd
          r6.x = asuint(-(g_split_distances[r6.y]) + g_split_distances[r6.x]); // 8642: add
          r8.y = asuint(-(g_fFadeRange.y) + 1.0f); // 8656: add
          r6.y = asuint(mad(asfloat(r6.x), asfloat(r8.y), g_split_distances[r6.y])); // 8665: mad
          r5.w = asuint(asfloat(r5.w) + -(asfloat(r6.y))); // 8677: add
          r6.x = asuint(asfloat(r6.x) * g_fFadeRange.y); // 8685: mul
          r5.w = asuint(saturate(asfloat(r5.w) / asfloat(r6.x))); // 8693: div
          r6.x = asuint(-(asfloat(r8.x)) + 1.0f); // 8700: add
          r8.x = asuint(mad(asfloat(r5.w), asfloat(r6.x), asfloat(r8.x))); // 8708: mad
        } // 8717: endif
      } else { // 8718: else
        r5.w = asuint(asint(r6.z) + 4); // 8719: iadd
        r6.x = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m00, g_amHardSplit[(r5.w / 4u)]._m10, g_amHardSplit[(r5.w / 4u)]._m20, g_amHardSplit[(r5.w / 4u)]._m30))); // 8726: dp4
        r6.y = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m01, g_amHardSplit[(r5.w / 4u)]._m11, g_amHardSplit[(r5.w / 4u)]._m21, g_amHardSplit[(r5.w / 4u)]._m31))); // 8736: dp4
        r6.z = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m02, g_amHardSplit[(r5.w / 4u)]._m12, g_amHardSplit[(r5.w / 4u)]._m22, g_amHardSplit[(r5.w / 4u)]._m32))); // 8746: dp4
        r5.w = asuint(dot(asfloat(r7.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m03, g_amHardSplit[(r5.w / 4u)]._m13, g_amHardSplit[(r5.w / 4u)]._m23, g_amHardSplit[(r5.w / 4u)]._m33))); // 8756: dp4
        r5.w = asuint(1.0f / asfloat(r5.w)); // 8766: div
        r6.xyz = asuint(asfloat(r5.www) * asfloat(r6.xyz)); // 8776: mul
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r6.xyxy))); // 8783: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 8796: sample_c
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 8809: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8822: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r6.xyxy))); // 8829: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 8842: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8855: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 8862: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8875: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r6.xyxy))); // 8882: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 8895: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8908: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 8915: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8928: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r6.xyxy))); // 8935: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 8948: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8961: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 8968: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 8981: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r6.xyxy))); // 8988: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 9001: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 9014: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 9021: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r7.w)); // 9034: add
        r9.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r6.xyxy))); // 9041: mad
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r6.z))); // 9054: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 9067: add
        r6.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r6.z))); // 9074: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 9087: add
        r8.x = asuint(asfloat(r5.w) * 0.0833333358f); // 9094: mul
      } // 9101: endif
    } else { // 9102: else
      r8.x = 0x3f800000u; // 9103: mov
    } // 9108: endif
    r6.xyz = asuint(asfloat(r7.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 9109: add
    r5.w = asuint(dot(asfloat(r6.xyz), asfloat(r6.xyz))); // 9118: dp3
    r5.w = asuint(sqrt(asfloat(r5.w))); // 9125: sqrt
    r6.x = (asfloat(r5.w) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 9130: ge
    if (r6.x != 0u) { // 9138: if
      r6.xyz = asuint(mad(asfloat(r7.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 9141: mad
      r6.xy = asuint(mad(asfloat(r7.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r6.xy))); // 9152: mad
      r7.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 9162: lt
      if (r7.w != 0u) { // 9170: if
        uint4 dimensions_9173 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_9173.x, dimensions_9173.y, dimensions_9173.w);
        r8.yz = asuint((float2)(dimensions_9173.xy)); // 9173: resinfo
        r9.xy = asuint(asfloat(r6.xy) * asfloat(r8.yz)); // 9182: mul
        r9.xy = asuint(floor(asfloat(r9.xy))); // 9189: round_ni
        r9.xy = asuint((int2)(asfloat(r9.xy))); // 9194: ftoi
        r9.zw = asuint((int2)(asfloat(r8.yz))); // 9199: ftoi
        r9.zw = asuint(asint(r9.zw) + int2(-1, -1)); // 9204: iadd
        r10.xy = r9.zw  & r9.xy; // 9214: and
        r10.zw = uint2(0x00000000u, 0x00000000u); // 9221: mov
        r11.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r10.xyw)).xyzw); // 9229: ld
        r9.xy = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r11.xy))); // 9238: mad
        r9.xy = asuint(mad(asfloat(r7.yy), asfloat(r11.zw), asfloat(r9.xy))); // 9248: mad
        r8.yz = asuint(asfloat(r8.yz) * asfloat(r9.xy)); // 9257: mul
        r8.yz = asuint(floor(asfloat(r8.yz))); // 9264: round_ni
        r8.yz = asuint((int2)(asfloat(r8.yz))); // 9269: ftoi
        r11.xy = r9.zw  & r8.yz; // 9274: and
        r8.yz = (asint(r10.xy) != asint(r11.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 9281: ine
        r7.w = r8.z  | r8.y; // 9288: or
        if (r7.w != 0u) { // 9295: if
          r11.zw = uint2(0x00000000u, 0x00000000u); // 9298: mov
          r10.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r11.xyw)).xyzw); // 9306: ld
          r7.xz = asuint(mad(asfloat(r7.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r10.xy))); // 9315: mad
          r9.xy = asuint(mad(asfloat(r7.yy), asfloat(r10.zw), asfloat(r7.xz))); // 9325: mad
        } // 9334: endif
        r7.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r6.z))); // 9335: sample_c_lz
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r6.z), int2(-1, 0))); // 9348: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 9362: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r6.z), int2(1, 0))); // 9369: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 9383: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r6.z), int2(0, -1))); // 9390: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 9404: add
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r9.xy), asfloat(r6.z), int2(0, 1))); // 9411: sample_c_lz
        r7.x = asuint(asfloat(r7.y) + asfloat(r7.x)); // 9425: add
        r7.x = asuint(asfloat(r7.x) * 0.200000003f); // 9432: mul
      } else { // 9439: else
        r7.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r6.z))); // 9440: sample_c_lz
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r6.z), int2(-1, 0))); // 9453: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 9467: add
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r6.z), int2(1, 0))); // 9474: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 9488: add
        r7.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r6.z), int2(0, -1))); // 9495: sample_c_lz
        r7.y = asuint(asfloat(r7.z) + asfloat(r7.y)); // 9509: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r6.xy), asfloat(r6.z), int2(0, 1))); // 9516: sample_c_lz
        r6.x = asuint(asfloat(r6.x) + asfloat(r7.y)); // 9530: add
        r7.x = asuint(asfloat(r6.x) * 0.200000003f); // 9537: mul
      } // 9544: endif
      r6.x = asuint(asfloat(r5.w) * abs(g_vBakedShadowmapParams.x)); // 9545: mul
      r6.x = asuint(min(asfloat(r6.x), 1.0f)); // 9554: min
      r6.x = asuint(asfloat(r6.x) * asfloat(r6.x)); // 9561: mul
      r6.x = asuint(mad(asfloat(r6.x), asfloat(r6.x), asfloat(r7.x))); // 9568: mad
      r5.w = asuint(mad(asfloat(r5.w), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 9577: mad
      r5.w = asuint(saturate(mad(asfloat(r5.w), 2.0f, -1.0f))); // 9588: mad
      r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 9597: add
      r5.w = asuint(min(asfloat(r5.w), 1.0f)); // 9604: min
      r8.x = asuint(min(asfloat(r5.w), asfloat(r8.x))); // 9611: min
    } // 9618: endif
    r8.x = asuint(saturate(asfloat(r8.x))); // 9619: mov
    r1.y = asuint(asfloat(r1.y) + asfloat(r8.x)); // 9624: add
    r6.xyz = asuint(mad(-(asfloat(r6.www)), asfloat(r4.xyz), v6.xyz)); // 9631: mad
    r6.xyz = asuint(mad(float3(view._m00, view._m10, view._m20), float3(0.109115697f, 0.109115697f, 0.109115697f), asfloat(r6.xyz))); // 9641: mad
    r6.xyz = asuint(mad(float3(view._m01, view._m11, view._m21), float3(-0.049937699f, -0.049937699f, -0.049937699f), asfloat(r6.xyz))); // 9654: mad
    r6.w = 0x3f800000u; // 9667: mov
    r5.w = asuint(dot(asfloat(r6.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 9672: dp4
    r6.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), asfloat(r6.xyz))); // 9680: mad
    r7.x = (asfloat(r5.w) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 9692: lt
    if (r7.x != 0u) { // 9700: if
      r7.x = asuint(g_split_distances[1]); // 9703: mov
      r7.y = asuint(g_split_distances[2]); // 9709: mov
      r7.z = asuint(g_split_distances[3]); // 9715: mov
      r7.w = asuint(g_split_distances[4]); // 9721: mov
      r7.xyzw = (asfloat(r5.wwww) >= asfloat(r7.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 9727: ge
      r7.xyzw = r7.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 9734: and
      r7.x = asuint(dot(asfloat(r7.xyzw), asfloat(r7.xyzw))); // 9744: dp4
      r7.x = asuint(asfloat(r7.x) + 0.5f); // 9751: add
      r7.x = asuint(floor(asfloat(r7.x))); // 9758: round_ni
      r7.x = asuint(min(asfloat(r4.w), asfloat(r7.x))); // 9763: min
      r7.y = asuint((int)(asfloat(r7.x))); // 9770: ftoi
      r7.z = r7.y << (0x00000002u & 31u); // 9775: ishl
      r6.w = 0x3f800000u; // 9782: mov
      r8.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.z / 4u)]._m00, g_amHardSplit[(r7.z / 4u)]._m10, g_amHardSplit[(r7.z / 4u)]._m20, g_amHardSplit[(r7.z / 4u)]._m30))); // 9787: dp4
      r8.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.z / 4u)]._m01, g_amHardSplit[(r7.z / 4u)]._m11, g_amHardSplit[(r7.z / 4u)]._m21, g_amHardSplit[(r7.z / 4u)]._m31))); // 9797: dp4
      r8.z = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.z / 4u)]._m02, g_amHardSplit[(r7.z / 4u)]._m12, g_amHardSplit[(r7.z / 4u)]._m22, g_amHardSplit[(r7.z / 4u)]._m32))); // 9807: dp4
      r7.w = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.z / 4u)]._m03, g_amHardSplit[(r7.z / 4u)]._m13, g_amHardSplit[(r7.z / 4u)]._m23, g_amHardSplit[(r7.z / 4u)]._m33))); // 9817: dp4
      r7.w = asuint(1.0f / asfloat(r7.w)); // 9827: div
      r9.xyz = asuint(asfloat(r7.www) * asfloat(r8.xyz)); // 9837: mul
      r8.z = asuint(g_iSplitCount * 0.5f); // 9844: mul
      r10.y = asuint(ceil(asfloat(r8.z))); // 9852: round_pi
      r10.x = 0x40000000u; // 9857: mov
      r8.z = r7.y  & 0x00000001u; // 9862: and
      r8.w = asuint(asint(r7.y) >> (0x00000001u & 31u)); // 9869: ishr
      r8.w = r8.w  & 0x00000001u; // 9876: and
      r11.xy = asuint((float2)(asint(r8.zw))); // 9883: itof
      r8.zw = asuint(mad(asfloat(r9.xy), asfloat(r10.xy), -(asfloat(r11.xy)))); // 9888: mad
      r8.zw = asuint(mad(asfloat(r8.zw), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 9898: mad
      r9.xy = asuint(saturate(asfloat(r8.zw))); // 9913: mov
      r8.zw = (asfloat(r8.zw) == asfloat(r9.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 9918: eq
      r7.x = asuint(trunc(asfloat(r7.x))); // 9925: round_z
      r7.x = (asfloat(r4.w) == asfloat(r7.x)) ? 0xffffffffu : 0u; // 9930: eq
      r8.z = r8.w  & r8.z; // 9937: and
      r8.w = (asint(r7.x) == -1) ? 0xffffffffu : 0u; // 9944: ieq
      r8.z = r8.w  | r8.z; // 9951: or
      if (r8.z != 0u) { // 9958: if
        r10.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 9961: mul
        r11.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r10.xyzw))); // 9972: mad
        r8.z = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.xy), asfloat(r9.z))); // 9981: sample_c
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.zw), asfloat(r9.z))); // 9994: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10007: add
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 10014: mul
        r12.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r11.xyzw))); // 10025: mad
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r9.z))); // 10034: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10047: add
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r9.z))); // 10054: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10067: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 10074: mul
        r13.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r12.xyzw))); // 10085: mad
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r9.z))); // 10094: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10107: add
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r9.z))); // 10114: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10127: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 10134: mul
        r14.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r13.xyzw))); // 10145: mad
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r9.z))); // 10154: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10167: add
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r9.z))); // 10174: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10187: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 10194: mul
        r15.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r14.xyzw))); // 10205: mad
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r9.z))); // 10214: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10227: add
        r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r9.z))); // 10234: sample_c
        r8.z = asuint(asfloat(r8.z) + asfloat(r9.x)); // 10247: add
        r15.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 10254: mul
        r16.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.wwww), asfloat(r15.xyzw))); // 10265: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.xy), asfloat(r9.z))); // 10274: sample_c
        r7.w = asuint(asfloat(r7.w) + asfloat(r8.z)); // 10287: add
        r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r16.zw), asfloat(r9.z))); // 10294: sample_c
        r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 10307: add
        r7.w = asuint(asfloat(r7.w) * 0.0833333358f); // 10314: mul
        if (r7.x == 0u) { // 10321: if
          r7.x = asuint(asint(r7.z) + 4); // 10324: iadd
          r8.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.x / 4u)]._m00, g_amHardSplit[(r7.x / 4u)]._m10, g_amHardSplit[(r7.x / 4u)]._m20, g_amHardSplit[(r7.x / 4u)]._m30))); // 10331: dp4
          r8.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.x / 4u)]._m01, g_amHardSplit[(r7.x / 4u)]._m11, g_amHardSplit[(r7.x / 4u)]._m21, g_amHardSplit[(r7.x / 4u)]._m31))); // 10341: dp4
          r8.z = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.x / 4u)]._m02, g_amHardSplit[(r7.x / 4u)]._m12, g_amHardSplit[(r7.x / 4u)]._m22, g_amHardSplit[(r7.x / 4u)]._m32))); // 10351: dp4
          r7.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r7.x / 4u)]._m03, g_amHardSplit[(r7.x / 4u)]._m13, g_amHardSplit[(r7.x / 4u)]._m23, g_amHardSplit[(r7.x / 4u)]._m33))); // 10361: dp4
          r7.x = asuint(1.0f / asfloat(r7.x)); // 10371: div
          r8.z = asuint(asfloat(r7.x) * asfloat(r8.z)); // 10381: mul
          r9.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r10.xyzw))); // 10388: mad
          r9.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.xy), asfloat(r8.z))); // 10397: sample_c
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r9.zw), asfloat(r8.z))); // 10410: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10423: add
          r10.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r11.xyzw))); // 10430: mad
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 10439: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10452: add
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 10459: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10472: add
          r10.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r12.xyzw))); // 10479: mad
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 10488: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10501: add
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 10508: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10521: add
          r10.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r13.xyzw))); // 10528: mad
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 10537: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10550: add
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 10557: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10570: add
          r10.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r14.xyzw))); // 10577: mad
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 10586: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10599: add
          r9.y = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 10606: sample_c
          r9.x = asuint(asfloat(r9.y) + asfloat(r9.x)); // 10619: add
          r10.xyzw = asuint(mad(asfloat(r8.xyxy), asfloat(r7.xxxx), asfloat(r15.xyzw))); // 10626: mad
          r7.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 10635: sample_c
          r7.x = asuint(asfloat(r7.x) + asfloat(r9.x)); // 10648: add
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 10655: sample_c
          r7.x = asuint(asfloat(r7.x) + asfloat(r8.x)); // 10668: add
          r8.x = asuint(asint(r7.y) + 1); // 10675: iadd
          r8.y = asuint(-(g_split_distances[r7.y]) + g_split_distances[r8.x]); // 10682: add
          r8.z = asuint(asfloat(r8.y) * g_fFadeRange.x); // 10696: mul
          r8.x = asuint(mad(-(asfloat(r8.y)), g_fFadeRange.x, g_split_distances[r8.x])); // 10704: mad
          r8.x = asuint(asfloat(r5.w) + -(asfloat(r8.x))); // 10718: add
          r8.x = asuint(saturate(asfloat(r8.x) / asfloat(r8.z))); // 10726: div
          r7.x = asuint(mad(asfloat(r7.x), 0.0833333358f, -(asfloat(r7.w)))); // 10733: mad
          r7.w = asuint(mad(asfloat(r8.x), asfloat(r7.x), asfloat(r7.w))); // 10743: mad
        } // 10752: endif
        if (r8.w != 0u) { // 10753: if
          r7.x = asuint(asint(r7.y) + 1); // 10756: iadd
          r7.x = asuint(-(g_split_distances[r7.y]) + g_split_distances[r7.x]); // 10763: add
          r8.x = asuint(-(g_fFadeRange.y) + 1.0f); // 10777: add
          r7.y = asuint(mad(asfloat(r7.x), asfloat(r8.x), g_split_distances[r7.y])); // 10786: mad
          r5.w = asuint(asfloat(r5.w) + -(asfloat(r7.y))); // 10798: add
          r7.x = asuint(asfloat(r7.x) * g_fFadeRange.y); // 10806: mul
          r5.w = asuint(saturate(asfloat(r5.w) / asfloat(r7.x))); // 10814: div
          r7.x = asuint(-(asfloat(r7.w)) + 1.0f); // 10821: add
          r7.w = asuint(mad(asfloat(r5.w), asfloat(r7.x), asfloat(r7.w))); // 10829: mad
        } // 10838: endif
      } else { // 10839: else
        r5.w = asuint(asint(r7.z) + 4); // 10840: iadd
        r7.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m00, g_amHardSplit[(r5.w / 4u)]._m10, g_amHardSplit[(r5.w / 4u)]._m20, g_amHardSplit[(r5.w / 4u)]._m30))); // 10847: dp4
        r7.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m01, g_amHardSplit[(r5.w / 4u)]._m11, g_amHardSplit[(r5.w / 4u)]._m21, g_amHardSplit[(r5.w / 4u)]._m31))); // 10857: dp4
        r7.z = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m02, g_amHardSplit[(r5.w / 4u)]._m12, g_amHardSplit[(r5.w / 4u)]._m22, g_amHardSplit[(r5.w / 4u)]._m32))); // 10867: dp4
        r5.w = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r5.w / 4u)]._m03, g_amHardSplit[(r5.w / 4u)]._m13, g_amHardSplit[(r5.w / 4u)]._m23, g_amHardSplit[(r5.w / 4u)]._m33))); // 10877: dp4
        r5.w = asuint(1.0f / asfloat(r5.w)); // 10887: div
        r7.xyz = asuint(asfloat(r5.www) * asfloat(r7.xyz)); // 10897: mul
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r7.xyxy))); // 10904: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 10917: sample_c
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 10930: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 10943: add
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r7.xyxy))); // 10950: mad
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 10963: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 10976: add
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 10983: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 10996: add
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r7.xyxy))); // 11003: mad
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 11016: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11029: add
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 11036: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11049: add
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r7.xyxy))); // 11056: mad
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 11069: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11082: add
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 11089: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11102: add
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r7.xyxy))); // 11109: mad
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 11122: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11135: add
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 11142: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11155: add
        r8.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r7.xyxy))); // 11162: mad
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r7.z))); // 11175: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11188: add
        r6.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r7.z))); // 11195: sample_c
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.w)); // 11208: add
        r7.w = asuint(asfloat(r5.w) * 0.0833333358f); // 11215: mul
      } // 11222: endif
    } else { // 11223: else
      r7.w = 0x3f800000u; // 11224: mov
    } // 11229: endif
    r7.xyz = asuint(asfloat(r6.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 11230: add
    r5.w = asuint(dot(asfloat(r7.xyz), asfloat(r7.xyz))); // 11239: dp3
    r5.w = asuint(sqrt(asfloat(r5.w))); // 11246: sqrt
    r6.w = (asfloat(r5.w) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 11251: ge
    if (r6.w != 0u) { // 11259: if
      r7.xyz = asuint(mad(asfloat(r6.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 11262: mad
      r7.xy = asuint(mad(asfloat(r6.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r7.xy))); // 11273: mad
      r6.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 11283: lt
      if (r6.w != 0u) { // 11291: if
        uint4 dimensions_11294 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_11294.x, dimensions_11294.y, dimensions_11294.w);
        r8.xy = asuint((float2)(dimensions_11294.xy)); // 11294: resinfo
        r8.zw = asuint(asfloat(r7.xy) * asfloat(r8.xy)); // 11303: mul
        r8.zw = asuint(floor(asfloat(r8.zw))); // 11310: round_ni
        r8.zw = asuint((int2)(asfloat(r8.zw))); // 11315: ftoi
        r9.xy = asuint((int2)(asfloat(r8.xy))); // 11320: ftoi
        r9.xy = asuint(asint(r9.xy) + int2(-1, -1)); // 11325: iadd
        r10.xy = r8.zw  & r9.xy; // 11335: and
        r10.zw = uint2(0x00000000u, 0x00000000u); // 11342: mov
        r11.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r10.xyw)).xyzw); // 11350: ld
        r8.zw = asuint(mad(asfloat(r6.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r11.xy))); // 11359: mad
        r8.zw = asuint(mad(asfloat(r6.yy), asfloat(r11.zw), asfloat(r8.zw))); // 11369: mad
        r8.xy = asuint(asfloat(r8.zw) * asfloat(r8.xy)); // 11378: mul
        r8.xy = asuint(floor(asfloat(r8.xy))); // 11385: round_ni
        r8.xy = asuint((int2)(asfloat(r8.xy))); // 11390: ftoi
        r9.xy = r9.xy  & r8.xy; // 11395: and
        r8.xy = (asint(r10.xy) != asint(r9.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 11402: ine
        r6.w = r8.y  | r8.x; // 11409: or
        if (r6.w != 0u) { // 11416: if
          r9.zw = uint2(0x00000000u, 0x00000000u); // 11419: mov
          r9.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r9.xyw)).xyzw); // 11427: ld
          r6.xz = asuint(mad(asfloat(r6.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r9.xy))); // 11436: mad
          r8.zw = asuint(mad(asfloat(r6.yy), asfloat(r9.zw), asfloat(r6.xz))); // 11446: mad
        } // 11455: endif
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.zw), asfloat(r7.z))); // 11456: sample_c_lz
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.zw), asfloat(r7.z), int2(-1, 0))); // 11469: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 11483: add
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.zw), asfloat(r7.z), int2(1, 0))); // 11490: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 11504: add
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.zw), asfloat(r7.z), int2(0, -1))); // 11511: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 11525: add
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r8.zw), asfloat(r7.z), int2(0, 1))); // 11532: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 11546: add
        r6.x = asuint(asfloat(r6.x) * 0.200000003f); // 11553: mul
      } else { // 11560: else
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.xy), asfloat(r7.z))); // 11561: sample_c_lz
        r6.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.xy), asfloat(r7.z), int2(-1, 0))); // 11574: sample_c_lz
        r6.y = asuint(asfloat(r6.z) + asfloat(r6.y)); // 11588: add
        r6.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.xy), asfloat(r7.z), int2(1, 0))); // 11595: sample_c_lz
        r6.y = asuint(asfloat(r6.z) + asfloat(r6.y)); // 11609: add
        r6.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.xy), asfloat(r7.z), int2(0, -1))); // 11616: sample_c_lz
        r6.y = asuint(asfloat(r6.z) + asfloat(r6.y)); // 11630: add
        r6.z = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.xy), asfloat(r7.z), int2(0, 1))); // 11637: sample_c_lz
        r6.y = asuint(asfloat(r6.z) + asfloat(r6.y)); // 11651: add
        r6.x = asuint(asfloat(r6.y) * 0.200000003f); // 11658: mul
      } // 11665: endif
      r6.y = asuint(asfloat(r5.w) * abs(g_vBakedShadowmapParams.x)); // 11666: mul
      r6.y = asuint(min(asfloat(r6.y), 1.0f)); // 11675: min
      r6.y = asuint(asfloat(r6.y) * asfloat(r6.y)); // 11682: mul
      r6.x = asuint(mad(asfloat(r6.y), asfloat(r6.y), asfloat(r6.x))); // 11689: mad
      r5.w = asuint(mad(asfloat(r5.w), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 11698: mad
      r5.w = asuint(saturate(mad(asfloat(r5.w), 2.0f, -1.0f))); // 11709: mad
      r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 11718: add
      r5.w = asuint(min(asfloat(r5.w), 1.0f)); // 11725: min
      r7.w = asuint(min(asfloat(r5.w), asfloat(r7.w))); // 11732: min
    } // 11739: endif
    r7.w = asuint(saturate(asfloat(r7.w))); // 11740: mov
    r1.y = asuint(asfloat(r1.y) + asfloat(r7.w)); // 11745: add
    r0.x = asuint(asfloat(r0.x) * 0.833333373f); // 11752: mul
    r4.xyz = asuint(mad(-(asfloat(r0.xxx)), asfloat(r4.xyz), v6.xyz)); // 11759: mad
    r4.xyz = asuint(mad(float3(view._m00, view._m10, view._m20), float3(0.0897708014f, 0.0897708014f, 0.0897708014f), asfloat(r4.xyz))); // 11769: mad
    r6.xyz = asuint(mad(float3(view._m01, view._m11, view._m21), float3(-0.120171599f, -0.120171599f, -0.120171599f), asfloat(r4.xyz))); // 11782: mad
    r6.w = 0x3f800000u; // 11795: mov
    r0.x = asuint(dot(asfloat(r6.xyzw), float4(view_projection._m03, view_projection._m13, view_projection._m23, view_projection._m33))); // 11800: dp4
    r6.xyz = asuint(mad(-(float3(g_shadow_light_direction.x, g_shadow_light_direction.y, g_shadow_light_direction.z)), float3(g_sample_bias, g_sample_bias, g_sample_bias), asfloat(r6.xyz))); // 11808: mad
    r4.x = (asfloat(r0.x) < g_vShadowRange.x) ? 0xffffffffu : 0u; // 11820: lt
    if (r4.x != 0u) { // 11828: if
      r7.x = asuint(g_split_distances[1]); // 11831: mov
      r7.y = asuint(g_split_distances[2]); // 11837: mov
      r7.z = asuint(g_split_distances[3]); // 11843: mov
      r7.w = asuint(g_split_distances[4]); // 11849: mov
      r7.xyzw = (asfloat(r0.xxxx) >= asfloat(r7.xyzw)) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 11855: ge
      r7.xyzw = r7.xyzw  & uint4(0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u); // 11862: and
      r4.x = asuint(dot(asfloat(r7.xyzw), asfloat(r7.xyzw))); // 11872: dp4
      r4.x = asuint(asfloat(r4.x) + 0.5f); // 11879: add
      r4.x = asuint(floor(asfloat(r4.x))); // 11886: round_ni
      r4.x = asuint(min(asfloat(r4.w), asfloat(r4.x))); // 11891: min
      r4.y = asuint((int)(asfloat(r4.x))); // 11898: ftoi
      r4.z = r4.y << (0x00000002u & 31u); // 11903: ishl
      r6.w = 0x3f800000u; // 11910: mov
      r7.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.z / 4u)]._m00, g_amHardSplit[(r4.z / 4u)]._m10, g_amHardSplit[(r4.z / 4u)]._m20, g_amHardSplit[(r4.z / 4u)]._m30))); // 11915: dp4
      r7.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.z / 4u)]._m01, g_amHardSplit[(r4.z / 4u)]._m11, g_amHardSplit[(r4.z / 4u)]._m21, g_amHardSplit[(r4.z / 4u)]._m31))); // 11925: dp4
      r7.z = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.z / 4u)]._m02, g_amHardSplit[(r4.z / 4u)]._m12, g_amHardSplit[(r4.z / 4u)]._m22, g_amHardSplit[(r4.z / 4u)]._m32))); // 11935: dp4
      r5.w = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.z / 4u)]._m03, g_amHardSplit[(r4.z / 4u)]._m13, g_amHardSplit[(r4.z / 4u)]._m23, g_amHardSplit[(r4.z / 4u)]._m33))); // 11945: dp4
      r5.w = asuint(1.0f / asfloat(r5.w)); // 11955: div
      r8.xyz = asuint(asfloat(r5.www) * asfloat(r7.xyz)); // 11965: mul
      r7.z = asuint(g_iSplitCount * 0.5f); // 11972: mul
      r9.y = asuint(ceil(asfloat(r7.z))); // 11980: round_pi
      r9.x = 0x40000000u; // 11985: mov
      r7.z = r4.y  & 0x00000001u; // 11990: and
      r7.w = asuint(asint(r4.y) >> (0x00000001u & 31u)); // 11997: ishr
      r7.w = r7.w  & 0x00000001u; // 12004: and
      r10.xy = asuint((float2)(asint(r7.zw))); // 12011: itof
      r7.zw = asuint(mad(asfloat(r8.xy), asfloat(r9.xy), -(asfloat(r10.xy)))); // 12016: mad
      r7.zw = asuint(mad(asfloat(r7.zw), float2(1.00199997f, 1.00199997f), float2(-0.00100000005f, -0.00100000005f))); // 12026: mad
      r8.xy = asuint(saturate(asfloat(r7.zw))); // 12041: mov
      r7.zw = (asfloat(r7.zw) == asfloat(r8.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 12046: eq
      r4.x = asuint(trunc(asfloat(r4.x))); // 12053: round_z
      r4.x = (asfloat(r4.w) == asfloat(r4.x)) ? 0xffffffffu : 0u; // 12058: eq
      r4.w = r7.w  & r7.z; // 12065: and
      r7.z = (asint(r4.x) == -1) ? 0xffffffffu : 0u; // 12072: ieq
      r4.w = r4.w  | r7.z; // 12079: or
      if (r4.w != 0u) { // 12086: if
        r9.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f)); // 12089: mul
        r10.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r9.xyzw))); // 12100: mad
        r4.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.xy), asfloat(r8.z))); // 12109: sample_c
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r10.zw), asfloat(r8.z))); // 12122: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12135: add
        r10.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f)); // 12142: mul
        r11.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r10.xyzw))); // 12153: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.xy), asfloat(r8.z))); // 12162: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12175: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r11.zw), asfloat(r8.z))); // 12182: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12195: add
        r11.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f)); // 12202: mul
        r12.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r11.xyzw))); // 12213: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.xy), asfloat(r8.z))); // 12222: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12235: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r12.zw), asfloat(r8.z))); // 12242: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12255: add
        r12.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f)); // 12262: mul
        r13.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r12.xyzw))); // 12273: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.xy), asfloat(r8.z))); // 12282: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12295: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r13.zw), asfloat(r8.z))); // 12302: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12315: add
        r13.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f)); // 12322: mul
        r14.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r13.xyzw))); // 12333: mad
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.xy), asfloat(r8.z))); // 12342: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12355: add
        r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r14.zw), asfloat(r8.z))); // 12362: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r7.w)); // 12375: add
        r14.xyzw = asuint(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w) * float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f)); // 12382: mul
        r15.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r5.wwww), asfloat(r14.xyzw))); // 12393: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.xy), asfloat(r8.z))); // 12402: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r5.w)); // 12415: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r15.zw), asfloat(r8.z))); // 12422: sample_c
        r4.w = asuint(asfloat(r4.w) + asfloat(r5.w)); // 12435: add
        r4.w = asuint(asfloat(r4.w) * 0.0833333358f); // 12442: mul
        if (r4.x == 0u) { // 12449: if
          r4.x = asuint(asint(r4.z) + 4); // 12452: iadd
          r7.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.x / 4u)]._m00, g_amHardSplit[(r4.x / 4u)]._m10, g_amHardSplit[(r4.x / 4u)]._m20, g_amHardSplit[(r4.x / 4u)]._m30))); // 12459: dp4
          r7.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.x / 4u)]._m01, g_amHardSplit[(r4.x / 4u)]._m11, g_amHardSplit[(r4.x / 4u)]._m21, g_amHardSplit[(r4.x / 4u)]._m31))); // 12469: dp4
          r5.w = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.x / 4u)]._m02, g_amHardSplit[(r4.x / 4u)]._m12, g_amHardSplit[(r4.x / 4u)]._m22, g_amHardSplit[(r4.x / 4u)]._m32))); // 12479: dp4
          r4.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r4.x / 4u)]._m03, g_amHardSplit[(r4.x / 4u)]._m13, g_amHardSplit[(r4.x / 4u)]._m23, g_amHardSplit[(r4.x / 4u)]._m33))); // 12489: dp4
          r4.x = asuint(1.0f / asfloat(r4.x)); // 12499: div
          r5.w = asuint(asfloat(r4.x) * asfloat(r5.w)); // 12509: mul
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r9.xyzw))); // 12516: mad
          r7.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12525: sample_c
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12538: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12551: add
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r10.xyzw))); // 12558: mad
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12567: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12580: add
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12587: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12600: add
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r11.xyzw))); // 12607: mad
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12616: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12629: add
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12636: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12649: add
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r12.xyzw))); // 12656: mad
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12665: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12678: add
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12685: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12698: add
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r13.xyzw))); // 12705: mad
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12714: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12727: add
          r8.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12734: sample_c
          r7.w = asuint(asfloat(r7.w) + asfloat(r8.x)); // 12747: add
          r8.xyzw = asuint(mad(asfloat(r7.xyxy), asfloat(r4.xxxx), asfloat(r14.xyzw))); // 12754: mad
          r4.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.xy), asfloat(r5.w))); // 12763: sample_c
          r4.x = asuint(asfloat(r4.x) + asfloat(r7.w)); // 12776: add
          r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r8.zw), asfloat(r5.w))); // 12783: sample_c
          r4.x = asuint(asfloat(r4.x) + asfloat(r5.w)); // 12796: add
          r5.w = asuint(asint(r4.y) + 1); // 12803: iadd
          r7.x = asuint(-(g_split_distances[r4.y]) + g_split_distances[r5.w]); // 12810: add
          r7.y = asuint(asfloat(r7.x) * g_fFadeRange.x); // 12824: mul
          r5.w = asuint(mad(-(asfloat(r7.x)), g_fFadeRange.x, g_split_distances[r5.w])); // 12832: mad
          r5.w = asuint(asfloat(r0.x) + -(asfloat(r5.w))); // 12846: add
          r5.w = asuint(saturate(asfloat(r5.w) / asfloat(r7.y))); // 12854: div
          r4.x = asuint(mad(asfloat(r4.x), 0.0833333358f, -(asfloat(r4.w)))); // 12861: mad
          r4.w = asuint(mad(asfloat(r5.w), asfloat(r4.x), asfloat(r4.w))); // 12871: mad
        } // 12880: endif
        if (r7.z != 0u) { // 12881: if
          r4.x = asuint(asint(r4.y) + 1); // 12884: iadd
          r4.x = asuint(-(g_split_distances[r4.y]) + g_split_distances[r4.x]); // 12891: add
          r5.w = asuint(-(g_fFadeRange.y) + 1.0f); // 12905: add
          r4.y = asuint(mad(asfloat(r4.x), asfloat(r5.w), g_split_distances[r4.y])); // 12914: mad
          r0.x = asuint(asfloat(r0.x) + -(asfloat(r4.y))); // 12926: add
          r4.x = asuint(asfloat(r4.x) * g_fFadeRange.y); // 12934: mul
          r0.x = asuint(saturate(asfloat(r0.x) / asfloat(r4.x))); // 12942: div
          r4.x = asuint(-(asfloat(r4.w)) + 1.0f); // 12949: add
          r4.w = asuint(mad(asfloat(r0.x), asfloat(r4.x), asfloat(r4.w))); // 12957: mad
        } // 12966: endif
      } else { // 12967: else
        r0.x = asuint(asint(r4.z) + 4); // 12968: iadd
        r4.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r0.x / 4u)]._m00, g_amHardSplit[(r0.x / 4u)]._m10, g_amHardSplit[(r0.x / 4u)]._m20, g_amHardSplit[(r0.x / 4u)]._m30))); // 12975: dp4
        r4.y = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r0.x / 4u)]._m01, g_amHardSplit[(r0.x / 4u)]._m11, g_amHardSplit[(r0.x / 4u)]._m21, g_amHardSplit[(r0.x / 4u)]._m31))); // 12985: dp4
        r4.z = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r0.x / 4u)]._m02, g_amHardSplit[(r0.x / 4u)]._m12, g_amHardSplit[(r0.x / 4u)]._m22, g_amHardSplit[(r0.x / 4u)]._m32))); // 12995: dp4
        r0.x = asuint(dot(asfloat(r6.xyzw), float4(g_amHardSplit[(r0.x / 4u)]._m03, g_amHardSplit[(r0.x / 4u)]._m13, g_amHardSplit[(r0.x / 4u)]._m23, g_amHardSplit[(r0.x / 4u)]._m33))); // 13005: dp4
        r0.x = asuint(1.0f / asfloat(r0.x)); // 13015: div
        r4.xyz = asuint(asfloat(r0.xxx) * asfloat(r4.xyz)); // 13025: mul
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.200000003f, 0.0f, -0.884842634f, 0.810588479f), asfloat(r4.xyxy))); // 13032: mad
        r0.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13045: sample_c
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13058: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13071: add
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.119481578f, -1.36143374f, 0.920573533f, 1.20072389f), asfloat(r4.xyxy))); // 13078: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13091: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13104: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13111: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13124: add
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-1.6200459f, -0.286562711f, 1.49071395f, -0.948270619f), asfloat(r4.xyxy))); // 13131: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13144: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13157: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13164: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13177: add
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(-0.488046318f, 1.81550848f, -0.915523231f, -1.76278055f), asfloat(r4.xyxy))); // 13184: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13197: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13210: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13217: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13230: add
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(1.96039701f, 0.71593231f, -2.01772094f, 0.832887232f), asfloat(r4.xyxy))); // 13237: mad
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13250: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13263: add
        r5.w = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13270: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r5.w)); // 13283: add
        r7.xyzw = asuint(mad(float4(g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w, g_vHardShadowBufferSize.z, g_vHardShadowBufferSize.w), float4(0.964031577f, -2.06008172f, 0.707034647f, 2.25413561f), asfloat(r4.xyxy))); // 13290: mad
        r4.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.xy), asfloat(r4.z))); // 13303: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r4.x)); // 13316: add
        r4.x = asuint(g_tShadowBuffer_SM50.SampleCmp(sShadowBuffer_SM50_s, asfloat(r7.zw), asfloat(r4.z))); // 13323: sample_c
        r0.x = asuint(asfloat(r0.x) + asfloat(r4.x)); // 13336: add
        r4.w = asuint(asfloat(r0.x) * 0.0833333358f); // 13343: mul
      } // 13350: endif
    } else { // 13351: else
      r4.w = 0x3f800000u; // 13352: mov
    } // 13357: endif
    r4.xyz = asuint(asfloat(r6.xyz) + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 13358: add
    r0.x = asuint(dot(asfloat(r4.xyz), asfloat(r4.xyz))); // 13367: dp3
    r0.x = asuint(sqrt(asfloat(r0.x))); // 13374: sqrt
    r4.x = (asfloat(r0.x) >= g_vBakedShadowmapParams.y) ? 0xffffffffu : 0u; // 13379: ge
    if (r4.x != 0u) { // 13387: if
      r4.xyz = asuint(mad(asfloat(r6.xzy), float3(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y, g_vBakedShadowmapScale.z), float3(g_vBakedShadowmapOffset.x, g_vBakedShadowmapOffset.y, g_vBakedShadowmapOffset.z))); // 13390: mad
      r4.xy = asuint(mad(asfloat(r6.yy), float2(g_vBakedShadowmapShear.x, g_vBakedShadowmapShear.y), asfloat(r4.xy))); // 13401: mad
      r5.w = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 13411: lt
      if (r5.w != 0u) { // 13419: if
        uint4 dimensions_13422 = uint4(0u, 0u, 0u, 0u);
        g_tBakedShadowDirs.GetDimensions(0x00000000u, dimensions_13422.x, dimensions_13422.y, dimensions_13422.w);
        r7.xy = asuint((float2)(dimensions_13422.xy)); // 13422: resinfo
        r7.zw = asuint(asfloat(r4.xy) * asfloat(r7.xy)); // 13431: mul
        r7.zw = asuint(floor(asfloat(r7.zw))); // 13438: round_ni
        r7.zw = asuint((int2)(asfloat(r7.zw))); // 13443: ftoi
        r8.xy = asuint((int2)(asfloat(r7.xy))); // 13448: ftoi
        r8.xy = asuint(asint(r8.xy) + int2(-1, -1)); // 13453: iadd
        r9.xy = r7.zw  & r8.xy; // 13463: and
        r9.zw = uint2(0x00000000u, 0x00000000u); // 13470: mov
        r10.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r9.xyw)).xyzw); // 13478: ld
        r7.zw = asuint(mad(asfloat(r6.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r10.xy))); // 13487: mad
        r7.zw = asuint(mad(asfloat(r6.yy), asfloat(r10.zw), asfloat(r7.zw))); // 13497: mad
        r7.xy = asuint(asfloat(r7.zw) * asfloat(r7.xy)); // 13506: mul
        r7.xy = asuint(floor(asfloat(r7.xy))); // 13513: round_ni
        r7.xy = asuint((int2)(asfloat(r7.xy))); // 13518: ftoi
        r8.xy = r8.xy  & r7.xy; // 13523: and
        r7.xy = (asint(r9.xy) != asint(r8.xy)) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 13530: ine
        r5.w = r7.y  | r7.x; // 13537: or
        if (r5.w != 0u) { // 13544: if
          r8.zw = uint2(0x00000000u, 0x00000000u); // 13547: mov
          r8.xyzw = asuint(g_tBakedShadowDirs.Load(asint(r8.xyw)).xyzw); // 13555: ld
          r6.xz = asuint(mad(asfloat(r6.xz), float2(g_vBakedShadowmapScale.x, g_vBakedShadowmapScale.y), asfloat(r8.xy))); // 13564: mad
          r7.zw = asuint(mad(asfloat(r6.yy), asfloat(r8.zw), asfloat(r6.xz))); // 13574: mad
        } // 13583: endif
        r5.w = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.zw), asfloat(r4.z))); // 13584: sample_c_lz
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.zw), asfloat(r4.z), int2(-1, 0))); // 13597: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 13611: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.zw), asfloat(r4.z), int2(1, 0))); // 13618: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 13632: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.zw), asfloat(r4.z), int2(0, -1))); // 13639: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 13653: add
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r7.zw), asfloat(r4.z), int2(0, 1))); // 13660: sample_c_lz
        r5.w = asuint(asfloat(r5.w) + asfloat(r6.x)); // 13674: add
        r5.w = asuint(asfloat(r5.w) * 0.200000003f); // 13681: mul
      } else { // 13688: else
        r6.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r4.xy), asfloat(r4.z))); // 13689: sample_c_lz
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r4.xy), asfloat(r4.z), int2(-1, 0))); // 13702: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 13716: add
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r4.xy), asfloat(r4.z), int2(1, 0))); // 13723: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 13737: add
        r6.y = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r4.xy), asfloat(r4.z), int2(0, -1))); // 13744: sample_c_lz
        r6.x = asuint(asfloat(r6.y) + asfloat(r6.x)); // 13758: add
        r4.x = asuint(g_tBakedShadowBuffer.SampleCmpLevelZero(sBakedShadowBuffer_s, asfloat(r4.xy), asfloat(r4.z), int2(0, 1))); // 13765: sample_c_lz
        r4.x = asuint(asfloat(r4.x) + asfloat(r6.x)); // 13779: add
        r5.w = asuint(asfloat(r4.x) * 0.200000003f); // 13786: mul
      } // 13793: endif
      r4.x = asuint(asfloat(r0.x) * abs(g_vBakedShadowmapParams.x)); // 13794: mul
      r4.x = asuint(min(asfloat(r4.x), 1.0f)); // 13803: min
      r4.x = asuint(asfloat(r4.x) * asfloat(r4.x)); // 13810: mul
      r4.x = asuint(mad(asfloat(r4.x), asfloat(r4.x), asfloat(r5.w))); // 13817: mad
      r0.x = asuint(mad(asfloat(r0.x), g_vBakedShadowmapParams.z, g_vBakedShadowmapParams.w)); // 13826: mad
      r0.x = asuint(saturate(mad(asfloat(r0.x), 2.0f, -1.0f))); // 13837: mad
      r0.x = asuint(asfloat(r0.x) + asfloat(r4.x)); // 13846: add
      r0.x = asuint(min(asfloat(r0.x), 1.0f)); // 13853: min
      r4.w = asuint(min(asfloat(r0.x), asfloat(r4.w))); // 13860: min
    } // 13867: endif
    r4.w = asuint(saturate(asfloat(r4.w))); // 13868: mov
    r0.x = asuint(asfloat(r1.y) + asfloat(r4.w)); // 13873: add
    r0.x = asuint(asfloat(r0.x) * asfloat(r1.x)); // 13880: mul
    r0.x = asuint(asfloat(r0.x) * 0.166666672f); // 13887: mul
  } else { // 13894: else
    r0.x = 0x3f800000u; // 13895: mov
  } // 13900: endif
  if (r0.z != 0u) { // 13901: if
    r4.xyzw = uint4(asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_average_size), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_additive_blend_mul), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_small_particles), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_large_particles)); // 13904: ld_structured
    r6.xyz = asuint(-(asfloat(r3.yyy)) * v8.xyz); // 13915: mul
    r6.xyz = asuint(mad(asfloat(r3.xxx), v7.xyz, asfloat(r6.xyz))); // 13923: mad
    r6.xyz = asuint(mad(asfloat(r3.www), v9.xyz, asfloat(r6.xyz))); // 13932: mad
    r6.w = (r6.z ^ 0x80000000u); // 13941: mov
    r7.x = asuint(dot(asfloat(r6.xyw), float3(inv_view._m00, inv_view._m10, inv_view._m20))); // 13947: dp3
    r7.y = asuint(dot(asfloat(r6.xyw), float3(inv_view._m01, inv_view._m11, inv_view._m21))); // 13955: dp3
    r7.z = asuint(dot(asfloat(r6.xyw), float3(inv_view._m02, inv_view._m12, inv_view._m22))); // 13963: dp3
    r0.z = r0.w  & 0x20000000u; // 13971: and
    r3.xyw = (r0.zzz != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x3f800000u, 0x00000000u) : r7.xyz; // 13978: movc
    r6.xyz = (asfloat(r3.xyw) < float3(0.0f, 0.0f, 0.0f)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 13990: lt
    r8.xyz = (r6.xxx != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_lr[1].x), asuint(ambient_cube_lr[1].y), asuint(ambient_cube_lr[1].z)) : uint3(asuint(ambient_cube_lr[0].x), asuint(ambient_cube_lr[0].y), asuint(ambient_cube_lr[0].z)); // 14000: movc
    r6.xyw = (r6.yyy != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_tb[1].x), asuint(ambient_cube_tb[1].y), asuint(ambient_cube_tb[1].z)) : uint3(asuint(ambient_cube_tb[0].x), asuint(ambient_cube_tb[0].y), asuint(ambient_cube_tb[0].z)); // 14011: movc
    r9.xyz = (r6.zzz != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_fb[1].x), asuint(ambient_cube_fb[1].y), asuint(ambient_cube_fb[1].z)) : uint3(asuint(ambient_cube_fb[0].x), asuint(ambient_cube_fb[0].y), asuint(ambient_cube_fb[0].z)); // 14022: movc
    r3.xyw = asuint(asfloat(r3.xyw) * asfloat(r3.xyw)); // 14033: mul
    r6.xyz = asuint(asfloat(r6.xyw) * asfloat(r3.yyy)); // 14040: mul
    r6.xyz = asuint(mad(asfloat(r3.xxx), asfloat(r8.xyz), asfloat(r6.xyz))); // 14047: mad
    r3.xyw = asuint(mad(asfloat(r3.www), asfloat(r9.xyz), asfloat(r6.xyz))); // 14056: mad
    r1.x = asuint(saturate(asfloat(r1.z))); // 14065: mov
    r6.xyz = asuint(asfloat(r4.yyy) * asfloat(r5.xyz)); // 14070: mul
    if (r0.z == 0u) { // 14077: if
      r8.x = asuint(inv_view._m20); // 14080: mov
      r8.y = asuint(inv_view._m21); // 14086: mov
      r8.z = asuint(inv_view._m22); // 14092: mov
      r1.y = asuint(dot(float3(sun_direction.x, sun_direction.y, sun_direction.z), asfloat(r8.xyz))); // 14098: dp3
      r5.w = asuint(asfloat(r4.w) + asfloat(r4.z)); // 14106: add
      r6.w = asuint(dot(-(float3(sun_direction.x, sun_direction.y, sun_direction.z)), asfloat(r7.xyz))); // 14113: dp3
      r7.x = asuint(asfloat(r6.w) * 0.699999988f); // 14122: mul
      r6.w = asuint(mad(asfloat(r6.w), -0.5f, 0.5f)); // 14129: mad
      r7.x = asuint(saturate(asfloat(r7.x))); // 14138: mov
      r6.w = (r3.z != 0u) ? r6.w : r7.x; // 14143: movc
      r6.w = asuint(-(asfloat(r5.w)) * asfloat(r6.w)); // 14152: mul
      r4.x = asuint(asfloat(r4.x) * asfloat(r6.w)); // 14160: mul
      r4.x = asuint(asfloat(r4.x) * v4.x); // 14167: mul
      r4.x = asuint(asfloat(r4.x) * 1.44269502f); // 14174: mul
      r4.x = asuint(exp2(asfloat(r4.x))); // 14181: exp
      r6.w = asuint(-(asfloat(r1.w)) + 1.0f); // 14186: add
      r7.x = asuint(mad(asfloat(r1.w), asfloat(r1.w), 1.0f)); // 14194: mad
      r1.w = asuint(dot(asfloat(r1.yy), asfloat(r1.ww))); // 14203: dp2
      r1.w = asuint(-(asfloat(r1.w)) + asfloat(r7.x)); // 14210: add
      r1.w = asuint(max(asfloat(r1.w), 0.0f)); // 14218: max
      r6.w = asuint(asfloat(r6.w) * asfloat(r6.w)); // 14225: mul
      r6.w = asuint(asfloat(r6.w) * 0.0795774683f); // 14232: mul
      r1.w = asuint(log2(asfloat(r1.w))); // 14239: log
      r1.w = asuint(asfloat(r1.w) * 1.5f); // 14244: mul
      r1.w = asuint(exp2(asfloat(r1.w))); // 14251: exp
      r1.w = asuint(asfloat(r6.w) / asfloat(r1.w)); // 14256: div
      r1.w = asuint(asfloat(r4.w) * asfloat(r1.w)); // 14263: mul
      r1.y = asuint(mad(asfloat(r1.y), asfloat(r1.y), 1.0f)); // 14270: mad
      r1.y = asuint(asfloat(r1.y) * asfloat(r4.z)); // 14279: mul
      r1.y = asuint(mad(asfloat(r1.y), 0.0596831031f, asfloat(r1.w))); // 14286: mad
      r1.y = asuint(asfloat(r1.y) / asfloat(r5.w)); // 14295: div
      r1.w = asuint(-(asfloat(r4.x)) + 1.0f); // 14302: add
      r4.z = asuint(asfloat(r1.w) * asfloat(r1.y)); // 14310: mul
      r1.y = asuint(mad(asfloat(r1.y), asfloat(r1.w), asfloat(r4.x))); // 14317: mad
      r1.y = (r3.z != 0u) ? r1.y : r4.z; // 14326: movc
      r4.xzw = asuint(asfloat(r1.yyy) * float3(sun_colour.x, sun_colour.y, sun_colour.z)); // 14335: mul
      r4.xzw = asuint(asfloat(r0.xxx) * asfloat(r4.xzw)); // 14343: mul
      r7.xyz = asuint(asfloat(r4.xzw) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 14350: mul
      r4.xzw = asuint(mad(-(asfloat(r4.xzw)), float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f), asfloat(r1.zzz))); // 14360: mad
      r4.xzw = asuint(mad(asfloat(r1.xxx), asfloat(r4.xzw), asfloat(r7.xyz))); // 14373: mad
      r7.xyz = asuint(asfloat(r2.xyz) * asfloat(r3.xyw)); // 14382: mul
      r4.xzw = asuint(mad(asfloat(r4.xzw), asfloat(r2.xyz), asfloat(r7.xyz))); // 14389: mad
      r2.xyz = asuint(mad(asfloat(r5.xyz), asfloat(r4.yyy), asfloat(r4.xzw))); // 14398: mad
    } else { // 14407: else
      r4.xyz = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(3.80762103e-05f, 3.80762103e-05f, 3.80762103e-05f)); // 14408: mul
      r4.xyz = (r0.zzz != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : r4.xyz; // 14419: movc
      r3.xyz = asuint(asfloat(r3.xyw) + asfloat(r4.xyz)); // 14431: add
      r1.yzw = asuint(asfloat(r1.zzz) + -(asfloat(r3.xyz))); // 14438: add
      r1.xyz = asuint(mad(asfloat(r1.xxx), asfloat(r1.yzw), asfloat(r3.xyz))); // 14446: mad
      r2.xyz = asuint(mad(asfloat(r1.xyz), asfloat(r2.xyz), asfloat(r6.xyz))); // 14455: mad
    } // 14464: endif
  } // 14465: endif
  // VFX BRIGHTNESS HOOK: after particle lighting, before all fog paths.
  // Multiply RGB only; retain particle alpha and the later soft-edge fade.
  r2.xyz = asuint(asfloat(r2.xyz) * PharaohVfxBrightness(v2.x));

  r0.x = r0.w  & asuint(g_draw_flags.x); // 14466: and
  r0.x = r0.x  & 0x40000000u; // 14474: and
  if (r0.x != 0u) { // 14481: if
    if (g_fog_mode == 0u) { // 14484: if
      r1.xyz = asuint(v6.xyz + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 14488: add
      r0.x = asuint(dot(asfloat(r1.xyz), asfloat(r1.xyz))); // 14497: dp3
      r0.x = asuint(sqrt(asfloat(r0.x))); // 14504: sqrt
      r0.z = asuint(saturate(g_legacy_fog_distance_start)); // 14509: mov
      r0.z = asuint(-(asfloat(r0.z)) + 1.0f); // 14515: add
      r0.z = asuint(mad(asfloat(r0.z), 8.0f, -4.0f)); // 14523: mad
      r0.w = asuint(saturate(g_legacy_fog_distance_strength)); // 14532: mov
      r0.w = asuint(-(asfloat(r0.w)) + 1.0f); // 14538: add
      r0.w = asuint(asfloat(r0.w) * 1000.0f); // 14546: mul
      r0.w = asuint(asfloat(r0.x) / asfloat(r0.w)); // 14553: div
      r0.z = asuint(asfloat(r0.w) + asfloat(r0.z)); // 14560: add
      r0.z = asuint(asfloat(r0.z) * 1.44269502f); // 14567: mul
      r0.z = asuint(exp2(asfloat(r0.z))); // 14574: exp
      r0.z = asuint(g_legacy_fog_distance_scale / asfloat(r0.z)); // 14579: div
      r0.z = asuint(saturate(-(asfloat(r0.z)) + g_legacy_fog_distance_scale)); // 14587: add
      r0.w = asuint(-(v6.y) + g_legacy_fog_height_top); // 14596: add
      r3.x = asuint(-(g_legacy_fog_height_bottom) + g_legacy_fog_height_top); // 14605: add
      r0.w = asuint(asfloat(r0.w) + -(g_legacy_fog_height_bottom)); // 14615: add
      r3.x = asuint(1.0f / asfloat(r3.x)); // 14624: div
      r0.w = asuint(saturate(asfloat(r0.w) * asfloat(r3.x))); // 14634: mul
      r3.x = asuint(mad(asfloat(r0.w), -2.0f, 3.0f)); // 14641: mad
      r0.w = asuint(asfloat(r0.w) * asfloat(r0.w)); // 14650: mul
      r0.w = asuint(asfloat(r0.w) * asfloat(r3.x)); // 14657: mul
      r3.x = asuint(dot(asfloat(r1.xz), asfloat(r1.xz))); // 14664: dp2
      r3.x = asuint(sqrt(asfloat(r3.x))); // 14671: sqrt
      r3.y = asuint(max(g_legacy_fog_clear_distance, 0.00100000005f)); // 14676: max
      r3.y = asuint(1.0f / asfloat(r3.y)); // 14684: div
      r3.x = asuint(asfloat(r3.y) * asfloat(r3.x)); // 14694: mul
      r3.x = asuint(min(asfloat(r3.x), 1.0f)); // 14701: min
      r3.y = asuint(mad(asfloat(r3.x), -2.0f, 3.0f)); // 14708: mad
      r3.x = asuint(asfloat(r3.x) * asfloat(r3.x)); // 14717: mul
      r3.x = asuint(asfloat(r3.x) * asfloat(r3.y)); // 14724: mul
      r0.z = asuint(mad(g_legacy_fog_height_strength, asfloat(r0.w), asfloat(r0.z))); // 14731: mad
      r3.yzw = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(g_legacy_volume_fog_colour.x, g_legacy_volume_fog_colour.y, g_legacy_volume_fog_colour.z)); // 14741: mul
      r3.yzw = asuint(asfloat(r3.yzw) * float3(1.5f, 1.5f, 1.5f)); // 14750: mul
      r3.yzw = asuint(asfloat(r3.yzw) * abs(float3(sun_direction.y, sun_direction.y, sun_direction.y))); // 14760: mul
      r4.xyz = asuint(asfloat(r3.yzw) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 14769: mul
      r1.w = asuint(max(asfloat(r1.y), 0.0f)); // 14779: max
      r1.xyz = asuint(t_sky.Sample(s_sky_s, asfloat(r1.xwz)).xyz); // 14786: sample
      r0.w = asuint(saturate(dot(asfloat(r0.zz), float2(g_legacy_fog_colour_blend, g_legacy_fog_colour_blend)))); // 14797: dp2
      r1.xyz = asuint(mad(-(asfloat(r3.yzw)), float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f), asfloat(r1.xyz))); // 14805: mad
      r1.xyz = asuint(mad(asfloat(r0.www), asfloat(r1.xyz), asfloat(r4.xyz))); // 14818: mad
      r0.z = asuint(saturate(asfloat(r0.z) * asfloat(r3.x))); // 14827: mul
      r0.w = asuint(-(g_legacy_force_fog.x) + g_legacy_force_fog.y); // 14834: add
      r0.x = asuint(asfloat(r0.x) + -(g_legacy_force_fog.x)); // 14844: add
      r0.w = asuint(1.0f / asfloat(r0.w)); // 14853: div
      r0.x = asuint(saturate(asfloat(r0.w) * asfloat(r0.x))); // 14863: mul
      r0.w = asuint(mad(asfloat(r0.x), -2.0f, 3.0f)); // 14870: mad
      r0.x = asuint(asfloat(r0.x) * asfloat(r0.x)); // 14879: mul
      r0.x = asuint(asfloat(r0.x) * asfloat(r0.w)); // 14886: mul
      r0.w = asuint(-(asfloat(r0.z)) + 1.0f); // 14893: add
      r0.x = asuint(mad(asfloat(r0.x), asfloat(r0.w), asfloat(r0.z))); // 14901: mad
      r1.xyz = asuint(mad(asfloat(r1.xyz), asfloat(r2.www), -(asfloat(r2.xyz)))); // 14910: mad
      r2.xyz = asuint(mad(asfloat(r0.xxx), asfloat(r1.xyz), asfloat(r2.xyz))); // 14920: mad
    } else { // 14929: else
      r0.x = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 14930: lt
      if (r0.x != 0u) { // 14938: if
        r0.xzw = asuint(v6.xyz + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 14941: add
        r0.x = asuint(dot(asfloat(r0.xzw), asfloat(r0.xzw))); // 14950: dp3
        r1.z = asuint(sqrt(asfloat(r0.x))); // 14957: sqrt
        r0.xz = asuint(asfloat(r0.wz) / asfloat(r1.zz)); // 14962: div
        r0.w = asuint(g_skybox_size * 0.5f); // 14969: mul
        r0.w = asuint(asfloat(r0.w) / abs(asfloat(r0.x))); // 14977: div
        r0.w = (asfloat(r0.w) < asfloat(r1.z)) ? 0xffffffffu : 0u; // 14985: lt
        r1.xw = (float2(9.99999997e-07f, 9.99999997e-07f) < abs(asfloat(r0.xz))) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 14992: lt
        r0.x = r0.w  & r1.x; // 15003: and
        r0.w = (camera_position.y < 200.0f) ? 0xffffffffu : 0u; // 15010: lt
        r0.x = r0.w  & r0.x; // 15018: and
        r3.y = asuint(mad(asfloat(r0.z), 100000.0f, camera_position.y)); // 15025: mad
        r3.z = 0x47c35000u; // 15035: mov
        r1.y = asuint(v6.y); // 15040: mov
        r3.yz = (r0.xx != uint2(0u, 0u)) ? r3.yz : r1.yz; // 15045: movc
        r0.x = asuint(min(g_fog_clear_distance, g_height_fog_clear_distance)); // 15054: min
        r0.x = asuint(min(asfloat(r0.x), asfloat(r3.z))); // 15063: min
        r0.w = asuint(mad(asfloat(r0.z), asfloat(r0.x), camera_position.y)); // 15070: mad
        r1.x = asuint(max(-(asfloat(r0.z)), 0.0f)); // 15080: max
        r1.y = asuint(asfloat(r1.x) * g_generating_light_probe); // 15088: mul
        r1.z = asuint(-(asfloat(r0.w)) + asfloat(r3.y)); // 15096: add
        r4.x = asuint(mad(asfloat(r1.y), asfloat(r1.z), asfloat(r0.w))); // 15104: mad
        r0.x = asuint(-(asfloat(r0.x)) + asfloat(r3.z)); // 15113: add
        r0.w = asuint(mad(-(asfloat(r1.x)), g_generating_light_probe, 1.0f)); // 15121: mad
        r1.z = asuint(asfloat(r0.w) * asfloat(r0.x)); // 15132: mul
        r3.z = asuint(-(g_fog_height_top) + g_fog_height_bottom); // 15139: add
        r3.z = asuint(mad(g_height_fog_falloff, asfloat(r3.z), g_fog_height_top)); // 15149: mad
        r4.w = asuint(-(asfloat(r4.x)) + asfloat(r3.z)); // 15160: add
        r4.w = asuint(asfloat(r4.w) / asfloat(r0.z)); // 15168: div
        r4.w = asuint(max(asfloat(r4.w), 0.0f)); // 15175: max
        r3.w = asuint(min(asfloat(r1.z), asfloat(r4.w))); // 15182: min
        r4.w = (9.99999997e-07f < asfloat(r0.z)) ? 0xffffffffu : 0u; // 15189: lt
        r3.x = asuint(max(asfloat(r3.z), asfloat(r4.x))); // 15196: max
        r5.x = (asfloat(r0.z) < -9.99999997e-07f) ? 0xffffffffu : 0u; // 15203: lt
        r4.y = asuint(max(asfloat(r3.z), asfloat(r3.y))); // 15210: max
        r4.z = asuint(mad(asfloat(r0.x), asfloat(r0.w), -(asfloat(r3.w)))); // 15217: mad
        r1.xy = uint2(0x00000000u, 0x00000000u); // 15227: mov
        r1.xyz = (r5.xxx != uint3(0u, 0u, 0u)) ? r4.xyz : r1.xyz; // 15235: movc
        r1.xyz = (r4.www != uint3(0u, 0u, 0u)) ? r3.xyw : r1.xyz; // 15244: movc
        r0.x = asuint(-(asfloat(r3.z)) + g_fog_height_top); // 15253: add
        r0.x = asuint(asfloat(r0.x) * 0.100000001f); // 15262: mul
        r0.w = (0.0f != asfloat(r0.x)) ? 0xffffffffu : 0u; // 15269: ne
        r0.w = r0.w  & r1.w; // 15279: and
        r1.xy = asuint(-(asfloat(r3.zz)) + asfloat(r1.xy)); // 15286: add
        r1.xy = asuint(max(asfloat(r1.xy), float2(0.0f, 0.0f))); // 15294: max
        r1.xy = asuint(-(asfloat(r1.xy)) / asfloat(r0.xx)); // 15304: div
        r1.xy = asuint(asfloat(r1.xy) * float2(1.44269502f, 1.44269502f)); // 15312: mul
        r1.xy = asuint(exp2(asfloat(r1.xy))); // 15322: exp
        r1.x = asuint(-(asfloat(r1.y)) + asfloat(r1.x)); // 15327: add
        r0.x = asuint(asfloat(r0.x) * asfloat(r1.x)); // 15335: mul
        r0.x = asuint(asfloat(r0.x) / asfloat(r0.z)); // 15342: div
        r0.x = asuint(asfloat(r0.x) + asfloat(r1.z)); // 15349: add
        r0.x = (r0.w != 0u) ? r0.x : r1.z; // 15356: movc
        r0.x = asuint(asfloat(r0.x) * g_fog_density_height); // 15365: mul
        r0.x = asuint(asfloat(r0.x) * -1.44269502f); // 15373: mul
        r0.x = asuint(exp2(asfloat(r0.x))); // 15380: exp
        r0.z = asuint(-(asfloat(r0.x)) + 1.0f); // 15385: add
        r1.xyz = asuint(asfloat(r0.zzz) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 15393: mul
        r1.xyz = asuint(asfloat(r2.www) * asfloat(r1.xyz)); // 15401: mul
        r2.xyz = asuint(mad(asfloat(r2.xyz), asfloat(r0.xxx), asfloat(r1.xyz))); // 15408: mad
      } else { // 15417: else
        r1.xyzw = asuint(v6.yxyz + -(float4(camera_position.y, camera_position.x, camera_position.y, camera_position.z))); // 15418: add
        r0.x = asuint(dot(asfloat(r1.yzw), asfloat(r1.yzw))); // 15427: dp3
        r0.x = asuint(sqrt(asfloat(r0.x))); // 15434: sqrt
        r1.xyzw = asuint(asfloat(r1.xyzw) / asfloat(r0.xxxx)); // 15439: div
        r0.z = asuint(min(g_fog_clear_distance, g_height_fog_clear_distance)); // 15446: min
        r0.w = asuint(mad(asfloat(r1.z), asfloat(r0.z), camera_position.y)); // 15455: mad
        r3.x = asuint(max(-(asfloat(r1.x)), 0.0f)); // 15465: max
        r3.x = asuint(asfloat(r3.x) * g_generating_light_probe); // 15473: mul
        r3.y = asuint(-(asfloat(r0.w)) + v6.y); // 15481: add
        r0.w = asuint(mad(asfloat(r3.x), asfloat(r3.y), asfloat(r0.w))); // 15489: mad
        r0.x = asuint(-(asfloat(r0.z)) + asfloat(r0.x)); // 15498: add
        r0.x = asuint(max(asfloat(r0.x), 0.0f)); // 15506: max
        r0.z = asuint(mad(g_fog_noise, -0.600000024f, 1.0f)); // 15513: mad
        r3.x = asuint(asfloat(r0.w) + -(g_atmosphere_bottom)); // 15523: add
        r3.y = asuint(max(asfloat(r3.x), 0.0f)); // 15532: max
        r3.z = asuint(asfloat(r3.y) + 6371000.0f); // 15539: add
        r3.w = asuint(mad(asfloat(r3.z), asfloat(r3.z), -4.07171624e+13f)); // 15546: mad
        r4.x = asuint(asfloat(r3.z) * -(sun_direction.y)); // 15555: mul
        r3.w = asuint(mad(asfloat(r4.x), asfloat(r4.x), -(asfloat(r3.w)))); // 15564: mad
        r3.w = asuint(sqrt(asfloat(r3.w))); // 15574: sqrt
        r3.z = asuint(mad(-(asfloat(r3.z)), -(sun_direction.y), asfloat(r3.w))); // 15579: mad
        r3.y = asuint(-(asfloat(r3.y)) + 10000.0f); // 15591: add
        r3.y = asuint(asfloat(r3.y) / asfloat(r3.z)); // 15599: div
        r3.y = asuint(min(asfloat(r3.y), 1.0f)); // 15606: min
        r3.z = asuint(-(g_fog_height_top) + g_fog_height_bottom); // 15613: add
        r3.z = asuint(mad(g_height_fog_falloff, asfloat(r3.z), g_fog_height_top)); // 15623: mad
        r3.w = (9.99999968e-21f < abs(asfloat(r3.y))) ? 0xffffffffu : 0u; // 15634: lt
        r4.x = asuint(max(asfloat(r0.w), g_fog_height_bottom)); // 15642: max
        r4.x = asuint(asfloat(r3.z) + -(asfloat(r4.x))); // 15650: add
        r4.y = asuint(asfloat(r4.x) / asfloat(r3.y)); // 15658: div
        r4.y = asuint(max(asfloat(r4.y), 0.0f)); // 15665: max
        r0.z = asuint(asfloat(r0.z) * g_fog_density_height); // 15672: mul
        r0.z = asuint(asfloat(r0.z) * 0.0125000002f); // 15680: mul
        r4.z = asuint(max(asfloat(r0.w), asfloat(r3.z))); // 15687: max
        r4.w = asuint(-(asfloat(r4.z)) + g_fog_height_top); // 15694: add
        r4.w = asuint(asfloat(r4.w) / asfloat(r3.y)); // 15703: div
        r4.w = asuint(max(asfloat(r4.w), 0.0f)); // 15710: max
        r4.yw = (r3.ww != uint2(0u, 0u)) ? r4.yw : uint2(0x49742400u, 0x49742400u); // 15717: movc
        r5.x = asuint(-(asfloat(r3.z)) + g_fog_height_top); // 15729: add
        r5.y = (0.0f != asfloat(r5.x)) ? 0xffffffffu : 0u; // 15738: ne
        r5.z = asuint(1.0f / asfloat(r5.x)); // 15748: div
        r5.y = r5.z  & r5.y; // 15758: and
        r4.z = asuint(min(asfloat(r4.z), g_fog_height_top)); // 15765: min
        r4.z = asuint(-(asfloat(r4.z)) + g_fog_height_top); // 15773: add
        r5.z = asuint(asfloat(r3.y) * 0.5f); // 15782: mul
        r4.z = asuint(mad(-(asfloat(r5.z)), asfloat(r4.w), asfloat(r4.z))); // 15789: mad
        r4.z = asuint(asfloat(r4.w) * asfloat(r4.z)); // 15799: mul
        r4.z = asuint(asfloat(r5.y) * asfloat(r4.z)); // 15806: mul
        r4.z = asuint(asfloat(r0.z) * asfloat(r4.z)); // 15813: mul
        r4.y = asuint(mad(asfloat(r0.z), asfloat(r4.y), asfloat(r4.z))); // 15820: mad
        r4.y = asuint(asfloat(r4.y) * 1.44269502f); // 15829: mul
        r4.y = asuint(exp2(asfloat(r4.y))); // 15836: exp
        r4.z = asuint(min(g_rayleigh_density.z, g_rayleigh_density.y)); // 15841: min
        r4.z = asuint(min(asfloat(r4.z), g_rayleigh_density.x)); // 15850: min
        r4.w = asuint(camera_position.y + -(g_atmosphere_bottom)); // 15858: add
        r4.w = asuint(max(asfloat(r4.w), 0.0f)); // 15868: max
        r6.xy = asuint(float2(g_atmosphere_height_multiplier, g_atmosphere_height_multiplier) * float2(6994.0f, 1200.0f)); // 15875: mul
        r6.zw = asuint(-(asfloat(r4.ww)) / asfloat(r6.xy)); // 15886: div
        r6.zw = asuint(asfloat(r6.zw) * float2(1.44269502f, 1.44269502f)); // 15894: mul
        r6.zw = asuint(exp2(asfloat(r6.zw))); // 15904: exp
        r6.zw = asuint(asfloat(r6.xy) * asfloat(r6.zw)); // 15909: mul
        r4.w = asuint(max(asfloat(r3.y), 0.0f)); // 15916: max
        r6.zw = asuint(asfloat(r6.zw) / asfloat(r4.ww)); // 15923: div
        r4.w = asuint(asfloat(r6.w) * 1.99999995e-05f); // 15930: mul
        r4.w = asuint(mad(asfloat(r4.z), asfloat(r6.z), asfloat(r4.w))); // 15937: mad
        r4.w = asuint(asfloat(r4.w) * 1.44269502f); // 15946: mul
        r4.w = asuint(exp2(asfloat(r4.w))); // 15953: exp
        r7.xyz = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 15958: mul
        r5.w = (asfloat(r0.w) < asfloat(r3.z)) ? 0xffffffffu : 0u; // 15969: lt
        r8.xyzw = (float4(9.99999968e-21f, 9.99999972e-10f, 9.99999997e-07f, 0.00100000005f) < abs(asfloat(r1.zzzz))) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 15976: lt
        r4.x = asuint(asfloat(r4.x) / asfloat(r1.z)); // 15987: div
        r4.x = asuint(max(asfloat(r4.x), 0.0f)); // 15994: max
        r4.x = (r8.x != 0u) ? r4.x : 0x49742400u; // 16001: movc
        r4.x = asuint(asfloat(r0.z) * asfloat(r4.x)); // 16010: mul
        r4.x = asuint(asfloat(r4.x) * -1.44269502f); // 16017: mul
        r4.x = asuint(exp2(asfloat(r4.x))); // 16024: exp
        r4.x = (r5.w != 0u) ? r4.x : 0x3f800000u; // 16029: movc
        r5.w = (asfloat(r3.z) < asfloat(r0.w)) ? 0xffffffffu : 0u; // 16038: lt
        r6.w = (asfloat(r0.w) >= asfloat(r3.z)) ? 0xffffffffu : 0u; // 16045: ge
        r7.w = (g_fog_height_top >= asfloat(r0.w)) ? 0xffffffffu : 0u; // 16052: ge
        r6.w = r6.w  & r7.w; // 16060: and
        r9.xy = asuint(-(asfloat(r0.ww)) + float2(g_fog_height_bottom, g_fog_height_top)); // 16067: add
        r9.xy = asuint(asfloat(r9.xy) / asfloat(r1.zz)); // 16076: div
        r9.xy = asuint(max(asfloat(r9.xy), float2(0.0f, 0.0f))); // 16083: max
        r7.w = asuint(min(asfloat(r9.y), 1000000.0f)); // 16093: min
        r8.x = r6.w  & 0x49742400u; // 16100: and
        r7.w = (r8.z != 0u) ? r7.w : r8.x; // 16107: movc
        r8.x = asuint(-(asfloat(r0.w)) + asfloat(r3.z)); // 16116: add
        r8.x = asuint(asfloat(r8.x) / asfloat(r1.z)); // 16124: div
        r8.x = asuint(max(asfloat(r8.x), 0.0f)); // 16131: max
        r9.z = asuint(min(asfloat(r8.x), 1000000.0f)); // 16138: min
        r9.z = r8.z  & r9.z; // 16145: and
        r9.w = asuint(max(asfloat(r7.w), asfloat(r9.z))); // 16152: max
        r7.w = asuint(min(asfloat(r7.w), asfloat(r9.z))); // 16159: min
        r9.z = asuint(mad(asfloat(r7.w), asfloat(r1.z), asfloat(r0.w))); // 16166: mad
        r7.w = asuint(-(asfloat(r7.w)) + asfloat(r9.w)); // 16175: add
        r9.z = asuint(-(asfloat(r9.z)) + g_fog_height_top); // 16183: add
        r10.xy = asuint(asfloat(r1.zz) * float2(0.5f, -0.5f)); // 16192: mul
        r9.z = asuint(mad(-(asfloat(r10.x)), asfloat(r7.w), asfloat(r9.z))); // 16202: mad
        r7.w = asuint(asfloat(r7.w) * asfloat(r9.z)); // 16212: mul
        r7.w = asuint(asfloat(r5.y) * asfloat(r7.w)); // 16219: mul
        r9.zw = r6.ww  | r8.yw; // 16226: or
        r7.w = asuint(asfloat(r0.z) * asfloat(r7.w)); // 16233: mul
        r7.w = r7.w  & r9.z; // 16240: and
        r5.w = r5.w  & r7.w; // 16247: and
        r7.w = (asfloat(r0.w) >= g_fog_height_bottom) ? 0xffffffffu : 0u; // 16254: ge
        r8.y = (asfloat(r3.z) >= asfloat(r0.w)) ? 0xffffffffu : 0u; // 16262: ge
        r7.w = r7.w  & r8.y; // 16269: and
        r8.x = asuint(min(asfloat(r0.x), asfloat(r8.x))); // 16276: min
        r8.y = r0.x  & r7.w; // 16283: and
        r8.y = (r8.z != 0u) ? r8.x : r8.y; // 16290: movc
        r9.xy = asuint(min(asfloat(r0.xx), asfloat(r9.xy))); // 16299: min
        r9.x = r8.z  & r9.x; // 16306: and
        r9.z = asuint(max(asfloat(r8.y), asfloat(r9.x))); // 16313: max
        r10.z = asuint(min(asfloat(r8.y), asfloat(r9.x))); // 16320: min
        r10.w = asuint(mad(asfloat(r10.z), asfloat(r1.z), asfloat(r0.w))); // 16327: mad
        r11.x = asuint(mad(asfloat(r9.z), asfloat(r1.z), asfloat(r0.w))); // 16336: mad
        r9.z = asuint(asfloat(r9.z) + -(asfloat(r10.z))); // 16345: add
        r10.z = asuint(asfloat(r3.z) + -(asfloat(r10.w))); // 16353: add
        r11.y = asuint(asfloat(r10.z) / asfloat(r3.y)); // 16361: div
        r10.w = asuint(-(asfloat(r10.w)) + g_fog_height_bottom); // 16368: add
        r11.z = asuint(asfloat(r10.w) / asfloat(r3.y)); // 16377: div
        r11.yz = asuint(max(asfloat(r11.yz), float2(0.0f, 0.0f))); // 16384: max
        r11.y = asuint(-(asfloat(r11.z)) + asfloat(r11.y)); // 16394: add
        r11.z = asuint(asfloat(r3.z) + -(asfloat(r11.x))); // 16402: add
        r11.w = asuint(asfloat(r11.z) / asfloat(r3.y)); // 16410: div
        r11.w = asuint(max(asfloat(r11.w), 0.0f)); // 16417: max
        r11.x = asuint(-(asfloat(r11.x)) + g_fog_height_bottom); // 16424: add
        r12.x = asuint(asfloat(r11.x) / asfloat(r3.y)); // 16433: div
        r12.x = asuint(max(asfloat(r12.x), 0.0f)); // 16440: max
        r11.w = asuint(asfloat(r11.w) + -(asfloat(r12.x))); // 16447: add
        r11.w = asuint(asfloat(r9.z) + asfloat(r11.w)); // 16455: add
        r11.w = asuint(-(asfloat(r11.y)) + asfloat(r11.w)); // 16462: add
        r12.xyz = asuint(asfloat(r9.zzz) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 16470: mul
        r12.w = (abs(asfloat(r11.w)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 16478: lt
        r13.x = (abs(asfloat(r9.z)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 16486: lt
        r12.w = r12.w  | r13.x; // 16494: or
        r11.y = asuint(-(asfloat(r0.z)) * asfloat(r11.y)); // 16501: mul
        r11.y = asuint(asfloat(r11.y) * 1.44269502f); // 16509: mul
        r11.y = asuint(exp2(asfloat(r11.y))); // 16516: exp
        r11.y = asuint(asfloat(r11.y) / asfloat(r11.w)); // 16521: div
        r11.w = asuint(-(asfloat(r0.z)) * asfloat(r11.w)); // 16528: mul
        r11.w = asuint(asfloat(r11.w) * 1.44269502f); // 16536: mul
        r11.w = asuint(exp2(asfloat(r11.w))); // 16543: exp
        r11.w = asuint(-(asfloat(r11.w)) + 1.0f); // 16548: add
        r11.y = asuint(asfloat(r11.w) * asfloat(r11.y)); // 16556: mul
        r11.y = (r12.w != 0u) ? r0.z : r11.y; // 16563: movc
        r13.yzw = asuint(asfloat(r11.yyy) * asfloat(r12.xyz)); // 16572: mul
        r7.w = r8.w  | r7.w; // 16579: or
        r13.yzw = r13.yzw  & r7.www; // 16586: and
        r6.w = r0.x  & r6.w; // 16593: and
        r6.w = (r8.z != 0u) ? r9.y : r6.w; // 16600: movc
        r8.x = r8.x  & r8.z; // 16609: and
        r8.z = asuint(max(asfloat(r6.w), asfloat(r8.x))); // 16616: max
        r6.w = asuint(min(asfloat(r6.w), asfloat(r8.x))); // 16623: min
        r8.x = asuint(mad(asfloat(r6.w), asfloat(r1.z), asfloat(r0.w))); // 16630: mad
        r8.w = asuint(mad(asfloat(r8.z), asfloat(r1.z), asfloat(r0.w))); // 16639: mad
        r6.w = asuint(-(asfloat(r6.w)) + asfloat(r8.z)); // 16648: add
        r8.z = asuint(-(asfloat(r8.x)) + g_fog_height_top); // 16656: add
        r9.y = asuint(asfloat(r8.z) / asfloat(r3.y)); // 16665: div
        r9.y = asuint(max(asfloat(r9.y), 0.0f)); // 16672: max
        r8.x = asuint(asfloat(r3.z) + -(asfloat(r8.x))); // 16679: add
        r11.y = asuint(asfloat(r8.x) / asfloat(r3.y)); // 16687: div
        r11.y = asuint(max(asfloat(r11.y), 0.0f)); // 16694: max
        r9.y = asuint(asfloat(r9.y) + -(asfloat(r11.y))); // 16701: add
        r11.y = asuint(-(asfloat(r8.w)) + g_fog_height_top); // 16709: add
        r11.w = asuint(asfloat(r11.y) / asfloat(r3.y)); // 16718: div
        r11.w = asuint(max(asfloat(r11.w), 0.0f)); // 16725: max
        r8.w = asuint(asfloat(r3.z) + -(asfloat(r8.w))); // 16732: add
        r12.w = asuint(asfloat(r8.w) / asfloat(r3.y)); // 16740: div
        r12.w = asuint(max(asfloat(r12.w), 0.0f)); // 16747: max
        r11.w = asuint(asfloat(r11.w) + -(asfloat(r12.w))); // 16754: add
        r11.w = asuint(-(asfloat(r9.y)) + asfloat(r11.w)); // 16762: add
        r11.w = asuint(asfloat(r11.w) / asfloat(r6.w)); // 16770: div
        r12.w = asuint(asfloat(r0.z) / asfloat(r5.x)); // 16777: div
        r14.x = asuint(mad(-(asfloat(r5.z)), asfloat(r9.y), asfloat(r8.z))); // 16784: mad
        r14.y = asuint(mad(-(asfloat(r5.z)), asfloat(r11.w), -(asfloat(r1.x)))); // 16794: mad
        r14.z = asuint(mad(asfloat(r11.w), asfloat(r14.y), asfloat(r10.y))); // 16805: mad
        r14.z = asuint(asfloat(r12.w) * asfloat(r14.z)); // 16814: mul
        r14.y = asuint(mad(asfloat(r9.y), asfloat(r14.y), asfloat(r8.z))); // 16821: mad
        r11.w = asuint(mad(asfloat(r11.w), asfloat(r14.x), asfloat(r14.y))); // 16830: mad
        r11.w = asuint(asfloat(r12.w) * asfloat(r11.w)); // 16839: mul
        r9.y = asuint(asfloat(r9.y) * asfloat(r14.x)); // 16846: mul
        r9.y = asuint(asfloat(r12.w) * asfloat(r9.y)); // 16853: mul
        r14.x = asuint(asfloat(r8.z) * asfloat(r12.w)); // 16860: mul
        r14.y = asuint(-(asfloat(r1.x)) * asfloat(r12.w)); // 16867: mul
        r14.w = asuint(sqrt(abs(asfloat(r14.z)))); // 16875: sqrt
        r15.x = asuint(mad(asfloat(r14.z), asfloat(r6.w), asfloat(r11.w))); // 16881: mad
        r15.x = asuint(-(asfloat(r6.w)) * asfloat(r15.x)); // 16890: mul
        r15.x = asuint(asfloat(r15.x) * 1.44269502f); // 16898: mul
        r15.x = asuint(exp2(asfloat(r15.x))); // 16905: exp
        r9.y = asuint(asfloat(r9.y) * -1.44269502f); // 16910: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 16917: exp
        r15.y = asuint(asfloat(r14.z) * 4.0f); // 16922: mul
        r15.z = asuint(1.0f / asfloat(r15.y)); // 16929: div
        r15.w = asuint(asfloat(r14.w) + asfloat(r14.w)); // 16939: add
        r15.w = asuint(1.0f / asfloat(r15.w)); // 16946: div
        r16.x = asuint(asfloat(r11.w) * asfloat(r15.w)); // 16956: mul
        r16.y = asuint(asfloat(r14.z) + asfloat(r14.z)); // 16963: add
        r16.z = asuint(mad(asfloat(r16.y), asfloat(r6.w), asfloat(r11.w))); // 16970: mad
        r15.w = asuint(asfloat(r15.w) * asfloat(r16.z)); // 16979: mul
        r16.w = (0.0f < asfloat(r14.z)) ? 0xffffffffu : 0u; // 16986: lt
        if (r16.w != 0u) { // 16993: if
          r16.w = asuint(abs(asfloat(r16.x)) * abs(asfloat(r16.x))); // 16996: mul
          r17.x = asuint(abs(asfloat(r16.x)) * asfloat(r16.w)); // 17005: mul
          r17.y = asuint(abs(asfloat(r16.x)) * asfloat(r17.x)); // 17013: mul
          r17.z = asuint(mad(abs(asfloat(r16.x)), 0.278393f, 1.0f)); // 17021: mad
          r16.w = asuint(mad(asfloat(r16.w), 0.230388999f, asfloat(r17.z))); // 17031: mad
          r16.w = asuint(mad(asfloat(r17.x), 0.000972000009f, asfloat(r16.w))); // 17040: mad
          r16.w = asuint(mad(asfloat(r17.y), 0.0781079978f, asfloat(r16.w))); // 17049: mad
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 17058: mul
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 17065: mul
          r16.w = asuint(1.0f / asfloat(r16.w)); // 17072: div
          r16.w = asuint(-(asfloat(r16.w)) + 1.0f); // 17082: add
          r17.x = (0.0f < asfloat(r16.x)) ? 0xffffffffu : 0u; // 17090: lt
          r17.y = (asfloat(r16.x) < 0.0f) ? 0xffffffffu : 0u; // 17097: lt
          r17.x = asuint(-(asint(r17.x)) + asint(r17.y)); // 17104: iadd
          r17.x = asuint((float)(asint(r17.x))); // 17112: itof
          r17.x = asuint(asfloat(r16.w) * asfloat(r17.x)); // 17117: mul
          r16.w = asuint(abs(asfloat(r15.w)) * abs(asfloat(r15.w))); // 17124: mul
          r17.z = asuint(abs(asfloat(r15.w)) * asfloat(r16.w)); // 17133: mul
          r17.w = asuint(abs(asfloat(r15.w)) * asfloat(r17.z)); // 17141: mul
          r18.x = asuint(mad(abs(asfloat(r15.w)), 0.278393f, 1.0f)); // 17149: mad
          r16.w = asuint(mad(asfloat(r16.w), 0.230388999f, asfloat(r18.x))); // 17159: mad
          r16.w = asuint(mad(asfloat(r17.z), 0.000972000009f, asfloat(r16.w))); // 17168: mad
          r16.w = asuint(mad(asfloat(r17.w), 0.0781079978f, asfloat(r16.w))); // 17177: mad
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 17186: mul
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 17193: mul
          r16.w = asuint(1.0f / asfloat(r16.w)); // 17200: div
          r16.w = asuint(-(asfloat(r16.w)) + 1.0f); // 17210: add
          r17.z = (0.0f < asfloat(r15.w)) ? 0xffffffffu : 0u; // 17218: lt
          r17.w = (asfloat(r15.w) < 0.0f) ? 0xffffffffu : 0u; // 17225: lt
          r17.z = asuint(-(asint(r17.z)) + asint(r17.w)); // 17232: iadd
          r17.z = asuint((float)(asint(r17.z))); // 17240: itof
          r17.y = asuint(asfloat(r16.w) * asfloat(r17.z)); // 17245: mul
        } else { // 17252: else
          r18.x = asuint(asfloat(r16.x) * asfloat(r16.x)); // 17253: mul
          r16.w = asuint(asfloat(r18.x) * 1.44269502f); // 17260: mul
          r16.w = asuint(exp2(asfloat(r16.w))); // 17267: exp
          r16.w = asuint(asfloat(r16.w) * 1.12837923f); // 17272: mul
          r18.y = asuint(asfloat(r18.x) * asfloat(r18.x)); // 17279: mul
          r18.zw = asuint(asfloat(r18.xy) * asfloat(r18.yy)); // 17286: mul
          r17.z = asuint(asfloat(r18.y) * asfloat(r18.z)); // 17293: mul
          r17.w = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r18.xyzw))); // 17300: dp4
          r17.w = asuint(asfloat(r17.w) + 1.0f); // 17310: add
          r18.x = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r18.xyzw))); // 17317: dp4
          r18.x = asuint(asfloat(r18.x) + 1.0f); // 17327: add
          r17.z = asuint(mad(asfloat(r17.z), 0.00257857703f, asfloat(r18.x))); // 17334: mad
          r16.x = asuint(asfloat(r16.x) * asfloat(r17.w)); // 17343: mul
          r16.x = asuint(asfloat(r16.x) / asfloat(r17.z)); // 17350: div
          r17.x = asuint(asfloat(r16.x) * asfloat(r16.w)); // 17357: mul
          r18.x = asuint(asfloat(r15.w) * asfloat(r15.w)); // 17364: mul
          r16.x = asuint(asfloat(r18.x) * 1.44269502f); // 17371: mul
          r16.x = asuint(exp2(asfloat(r16.x))); // 17378: exp
          r16.x = asuint(asfloat(r16.x) * 1.12837923f); // 17383: mul
          r18.y = asuint(asfloat(r18.x) * asfloat(r18.x)); // 17390: mul
          r18.zw = asuint(asfloat(r18.xy) * asfloat(r18.yy)); // 17397: mul
          r16.w = asuint(asfloat(r18.y) * asfloat(r18.z)); // 17404: mul
          r17.z = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r18.xyzw))); // 17411: dp4
          r17.w = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r18.xyzw))); // 17421: dp4
          r17.zw = asuint(asfloat(r17.zw) + float2(1.0f, 1.0f)); // 17431: add
          r16.w = asuint(mad(asfloat(r16.w), 0.00257857703f, asfloat(r17.w))); // 17441: mad
          r15.w = asuint(asfloat(r15.w) * asfloat(r17.z)); // 17450: mul
          r15.w = asuint(asfloat(r15.w) / asfloat(r16.w)); // 17457: div
          r17.y = asuint(asfloat(r15.w) * asfloat(r16.x)); // 17464: mul
        } // 17471: endif
        r15.w = asuint(asfloat(r11.w) * asfloat(r14.y)); // 17472: mul
        r16.x = asuint(mad(asfloat(r16.y), asfloat(r14.x), -(asfloat(r15.w)))); // 17479: mad
        r16.x = asuint(asfloat(r16.x) * 1.7724539f); // 17489: mul
        r16.w = asuint(asfloat(r11.w) * asfloat(r11.w)); // 17496: mul
        r17.z = asuint(asfloat(r15.z) * asfloat(r16.w)); // 17503: mul
        r17.z = asuint(asfloat(r17.z) * 1.44269502f); // 17510: mul
        r17.z = asuint(exp2(asfloat(r17.z))); // 17517: exp
        r17.z = asuint(asfloat(r16.x) * asfloat(r17.z)); // 17522: mul
        r17.x = asuint(asfloat(r17.x) * asfloat(r17.z)); // 17529: mul
        r16.z = asuint(asfloat(r16.z) * asfloat(r16.z)); // 17536: mul
        r15.z = asuint(asfloat(r15.z) * asfloat(r16.z)); // 17543: mul
        r15.z = asuint(asfloat(r15.z) * 1.44269502f); // 17550: mul
        r15.z = asuint(exp2(asfloat(r15.z))); // 17557: exp
        r15.z = asuint(asfloat(r15.z) * asfloat(r16.x)); // 17562: mul
        r15.z = asuint(asfloat(r17.y) * asfloat(r15.z)); // 17569: mul
        r14.w = asuint(asfloat(r14.w) * asfloat(r15.y)); // 17576: mul
        r15.y = asuint(asfloat(r14.y) / asfloat(r16.y)); // 17583: div
        r14.z = (9.99999975e-06f < abs(asfloat(r14.z))) ? 0xffffffffu : 0u; // 17590: lt
        r16.x = asuint(-(asfloat(r15.x)) + 1.0f); // 17598: add
        r16.y = asuint(asfloat(r15.y) * asfloat(r16.x)); // 17606: mul
        r15.x = asuint(mad(asfloat(r15.x), asfloat(r15.z), -(asfloat(r17.x)))); // 17613: mad
        r14.w = asuint(asfloat(r15.x) / asfloat(r14.w)); // 17623: div
        r14.w = asuint(mad(asfloat(r15.y), asfloat(r16.x), asfloat(r14.w))); // 17630: mad
        r15.x = (asfloat(r15.x) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 17639: lt
        r15.y = (9.99999997e-07f < abs(asfloat(r11.w))) ? 0xffffffffu : 0u; // 17646: lt
        r15.z = asuint(mad(asfloat(r11.w), asfloat(r14.x), asfloat(r14.y))); // 17654: mad
        r15.w = asuint(mad(asfloat(r15.w), asfloat(r6.w), asfloat(r15.z))); // 17663: mad
        r11.w = asuint(asfloat(r6.w) * -(asfloat(r11.w))); // 17672: mul
        r11.w = asuint(asfloat(r11.w) * 1.44269502f); // 17680: mul
        r11.w = asuint(exp2(asfloat(r11.w))); // 17687: exp
        r11.w = asuint(mad(-(asfloat(r15.w)), asfloat(r11.w), asfloat(r15.z))); // 17692: mad
        r11.w = asuint(asfloat(r11.w) / asfloat(r16.w)); // 17702: div
        r15.z = asuint(asfloat(r6.w) * asfloat(r14.y)); // 17709: mul
        r15.z = asuint(mad(asfloat(r15.z), 0.5f, asfloat(r14.x))); // 17716: mad
        r15.z = asuint(asfloat(r6.w) * asfloat(r15.z)); // 17725: mul
        r11.w = (r15.y != 0u) ? r11.w : r15.z; // 17732: movc
        r11.w = (r15.x != 0u) ? r16.y : r11.w; // 17741: movc
        r11.w = (r14.z != 0u) ? r14.w : r11.w; // 17750: movc
        r9.y = asuint(asfloat(r9.y) * asfloat(r11.w)); // 17759: mul
        r11.w = (1.0f < asfloat(r9.y)) ? 0xffffffffu : 0u; // 17766: lt
        r9.y = (r11.w != 0u) ? 0x3f800000u : r9.y; // 17773: movc
        r11.w = asuint(asfloat(r0.z) * asfloat(r5.x)); // 17782: mul
        r11.w = (0.0f < asfloat(r11.w)) ? 0xffffffffu : 0u; // 17789: lt
        r14.z = (0.0f < asfloat(r6.w)) ? 0xffffffffu : 0u; // 17796: lt
        r11.w = r11.w  & r14.z; // 17803: and
        r9.w = r9.w  & r11.w; // 17810: and
        r9.y = r9.y  & r9.w; // 17817: and
        r15.xyw = asuint(asfloat(r9.yyy) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 17824: mul
        r9.y = asuint(asfloat(r5.x) / asfloat(r3.y)); // 17832: div
        r9.y = asuint(max(asfloat(r9.y), 0.0f)); // 17839: max
        r9.y = (r3.w != 0u) ? r9.y : 0x49742400u; // 17846: movc
        r11.w = asuint(min(asfloat(r3.z), g_fog_height_top)); // 17855: min
        r11.w = asuint(-(asfloat(r11.w)) + g_fog_height_top); // 17863: add
        r14.z = asuint(mad(-(asfloat(r5.z)), asfloat(r9.y), asfloat(r11.w))); // 17872: mad
        r9.y = asuint(asfloat(r9.y) * asfloat(r14.z)); // 17882: mul
        r9.y = asuint(asfloat(r5.y) * asfloat(r9.y)); // 17889: mul
        r9.y = asuint(mad(asfloat(r0.z), asfloat(r9.y), asfloat(r5.w))); // 17896: mad
        r9.y = asuint(asfloat(r9.y) * -1.44269502f); // 17905: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 17912: exp
        r15.xyw = asuint(asfloat(r4.xxx) * asfloat(r15.xyw)); // 17917: mul
        r13.yzw = asuint(mad(asfloat(r13.yzw), asfloat(r9.yyy), asfloat(r15.xyw))); // 17924: mad
        r9.y = asuint(max(asfloat(r10.z), 0.0f)); // 17933: max
        r10.z = asuint(max(asfloat(r10.w), 0.0f)); // 17940: max
        r9.y = asuint(asfloat(r9.y) + -(asfloat(r10.z))); // 17947: add
        r10.zw = asuint(max(asfloat(r11.zx), float2(0.0f, 0.0f))); // 17955: max
        r10.z = asuint(-(asfloat(r10.w)) + asfloat(r10.z)); // 17965: add
        r9.z = asuint(asfloat(r9.z) + asfloat(r10.z)); // 17973: add
        r9.z = asuint(-(asfloat(r9.y)) + asfloat(r9.z)); // 17980: add
        r10.z = (abs(asfloat(r9.z)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 17988: lt
        r10.z = r13.x  | r10.z; // 17996: or
        r9.y = asuint(-(asfloat(r0.z)) * asfloat(r9.y)); // 18003: mul
        r9.y = asuint(asfloat(r9.y) * 1.44269502f); // 18011: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 18018: exp
        r9.y = asuint(asfloat(r9.y) / asfloat(r9.z)); // 18023: div
        r9.z = asuint(-(asfloat(r0.z)) * asfloat(r9.z)); // 18030: mul
        r9.z = asuint(asfloat(r9.z) * 1.44269502f); // 18038: mul
        r9.z = asuint(exp2(asfloat(r9.z))); // 18045: exp
        r9.z = asuint(-(asfloat(r9.z)) + 1.0f); // 18050: add
        r9.y = asuint(asfloat(r9.z) * asfloat(r9.y)); // 18058: mul
        r9.y = (r10.z != 0u) ? r0.z : r9.y; // 18065: movc
        r12.xyz = asuint(asfloat(r9.yyy) * asfloat(r12.xyz)); // 18074: mul
        r12.xyz = r7.www  & r12.xyz; // 18081: and
        r7.w = asuint(max(asfloat(r8.z), 0.0f)); // 18088: max
        r8.xw = asuint(max(asfloat(r8.xw), float2(0.0f, 0.0f))); // 18095: max
        r7.w = asuint(asfloat(r7.w) + -(asfloat(r8.x))); // 18105: add
        r8.x = asuint(max(asfloat(r11.y), 0.0f)); // 18113: max
        r8.x = asuint(-(asfloat(r8.w)) + asfloat(r8.x)); // 18120: add
        r8.x = asuint(-(asfloat(r7.w)) + asfloat(r8.x)); // 18128: add
        r8.x = asuint(asfloat(r8.x) / asfloat(r6.w)); // 18136: div
        r8.w = asuint(mad(-(asfloat(r7.w)), 0.5f, asfloat(r8.z))); // 18143: mad
        r1.x = asuint(mad(-(asfloat(r8.x)), 0.5f, -(asfloat(r1.x)))); // 18153: mad
        r9.y = asuint(mad(asfloat(r8.x), asfloat(r1.x), asfloat(r10.y))); // 18164: mad
        r9.y = asuint(asfloat(r12.w) * asfloat(r9.y)); // 18173: mul
        r1.x = asuint(mad(asfloat(r7.w), asfloat(r1.x), asfloat(r8.z))); // 18180: mad
        r1.x = asuint(mad(asfloat(r8.x), asfloat(r8.w), asfloat(r1.x))); // 18189: mad
        r1.x = asuint(asfloat(r12.w) * asfloat(r1.x)); // 18198: mul
        r7.w = asuint(asfloat(r7.w) * asfloat(r8.w)); // 18205: mul
        r7.w = asuint(asfloat(r12.w) * asfloat(r7.w)); // 18212: mul
        r8.x = asuint(sqrt(abs(asfloat(r9.y)))); // 18219: sqrt
        r8.w = asuint(mad(asfloat(r9.y), asfloat(r6.w), asfloat(r1.x))); // 18225: mad
        r8.w = asuint(-(asfloat(r6.w)) * asfloat(r8.w)); // 18234: mul
        r8.w = asuint(asfloat(r8.w) * 1.44269502f); // 18242: mul
        r8.w = asuint(exp2(asfloat(r8.w))); // 18249: exp
        r7.w = asuint(asfloat(r7.w) * -1.44269502f); // 18254: mul
        r7.w = asuint(exp2(asfloat(r7.w))); // 18261: exp
        r9.z = asuint(asfloat(r9.y) * 4.0f); // 18266: mul
        r10.y = asuint(1.0f / asfloat(r9.z)); // 18273: div
        r10.z = asuint(asfloat(r8.x) + asfloat(r8.x)); // 18283: add
        r10.z = asuint(1.0f / asfloat(r10.z)); // 18290: div
        r10.w = asuint(asfloat(r1.x) * asfloat(r10.z)); // 18300: mul
        r11.x = asuint(asfloat(r9.y) + asfloat(r9.y)); // 18307: add
        r11.y = asuint(mad(asfloat(r11.x), asfloat(r6.w), asfloat(r1.x))); // 18314: mad
        r10.z = asuint(asfloat(r10.z) * asfloat(r11.y)); // 18323: mul
        r11.z = (0.0f < asfloat(r9.y)) ? 0xffffffffu : 0u; // 18330: lt
        if (r11.z != 0u) { // 18337: if
          r11.z = asuint(abs(asfloat(r10.w)) * abs(asfloat(r10.w))); // 18340: mul
          r12.w = asuint(abs(asfloat(r10.w)) * asfloat(r11.z)); // 18349: mul
          r13.x = asuint(abs(asfloat(r10.w)) * asfloat(r12.w)); // 18357: mul
          r14.z = asuint(mad(abs(asfloat(r10.w)), 0.278393f, 1.0f)); // 18365: mad
          r11.z = asuint(mad(asfloat(r11.z), 0.230388999f, asfloat(r14.z))); // 18375: mad
          r11.z = asuint(mad(asfloat(r12.w), 0.000972000009f, asfloat(r11.z))); // 18384: mad
          r11.z = asuint(mad(asfloat(r13.x), 0.0781079978f, asfloat(r11.z))); // 18393: mad
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 18402: mul
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 18409: mul
          r11.z = asuint(1.0f / asfloat(r11.z)); // 18416: div
          r11.z = asuint(-(asfloat(r11.z)) + 1.0f); // 18426: add
          r12.w = (0.0f < asfloat(r10.w)) ? 0xffffffffu : 0u; // 18434: lt
          r13.x = (asfloat(r10.w) < 0.0f) ? 0xffffffffu : 0u; // 18441: lt
          r12.w = asuint(-(asint(r12.w)) + asint(r13.x)); // 18448: iadd
          r12.w = asuint((float)(asint(r12.w))); // 18456: itof
          r15.x = asuint(asfloat(r11.z) * asfloat(r12.w)); // 18461: mul
          r11.z = asuint(abs(asfloat(r10.z)) * abs(asfloat(r10.z))); // 18468: mul
          r12.w = asuint(abs(asfloat(r10.z)) * asfloat(r11.z)); // 18477: mul
          r13.x = asuint(abs(asfloat(r10.z)) * asfloat(r12.w)); // 18485: mul
          r14.z = asuint(mad(abs(asfloat(r10.z)), 0.278393f, 1.0f)); // 18493: mad
          r11.z = asuint(mad(asfloat(r11.z), 0.230388999f, asfloat(r14.z))); // 18503: mad
          r11.z = asuint(mad(asfloat(r12.w), 0.000972000009f, asfloat(r11.z))); // 18512: mad
          r11.z = asuint(mad(asfloat(r13.x), 0.0781079978f, asfloat(r11.z))); // 18521: mad
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 18530: mul
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 18537: mul
          r11.z = asuint(1.0f / asfloat(r11.z)); // 18544: div
          r11.z = asuint(-(asfloat(r11.z)) + 1.0f); // 18554: add
          r12.w = (0.0f < asfloat(r10.z)) ? 0xffffffffu : 0u; // 18562: lt
          r13.x = (asfloat(r10.z) < 0.0f) ? 0xffffffffu : 0u; // 18569: lt
          r12.w = asuint(-(asint(r12.w)) + asint(r13.x)); // 18576: iadd
          r12.w = asuint((float)(asint(r12.w))); // 18584: itof
          r15.y = asuint(asfloat(r11.z) * asfloat(r12.w)); // 18589: mul
        } else { // 18596: else
          r16.x = asuint(asfloat(r10.w) * asfloat(r10.w)); // 18597: mul
          r11.z = asuint(asfloat(r16.x) * 1.44269502f); // 18604: mul
          r11.z = asuint(exp2(asfloat(r11.z))); // 18611: exp
          r11.z = asuint(asfloat(r11.z) * 1.12837923f); // 18616: mul
          r16.y = asuint(asfloat(r16.x) * asfloat(r16.x)); // 18623: mul
          r16.zw = asuint(asfloat(r16.xy) * asfloat(r16.yy)); // 18630: mul
          r12.w = asuint(asfloat(r16.y) * asfloat(r16.z)); // 18637: mul
          r13.x = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r16.xyzw))); // 18644: dp4
          r13.x = asuint(asfloat(r13.x) + 1.0f); // 18654: add
          r14.z = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r16.xyzw))); // 18661: dp4
          r14.z = asuint(asfloat(r14.z) + 1.0f); // 18671: add
          r12.w = asuint(mad(asfloat(r12.w), 0.00257857703f, asfloat(r14.z))); // 18678: mad
          r10.w = asuint(asfloat(r10.w) * asfloat(r13.x)); // 18687: mul
          r10.w = asuint(asfloat(r10.w) / asfloat(r12.w)); // 18694: div
          r15.x = asuint(asfloat(r10.w) * asfloat(r11.z)); // 18701: mul
          r16.x = asuint(asfloat(r10.z) * asfloat(r10.z)); // 18708: mul
          r10.w = asuint(asfloat(r16.x) * 1.44269502f); // 18715: mul
          r10.w = asuint(exp2(asfloat(r10.w))); // 18722: exp
          r10.w = asuint(asfloat(r10.w) * 1.12837923f); // 18727: mul
          r16.y = asuint(asfloat(r16.x) * asfloat(r16.x)); // 18734: mul
          r16.zw = asuint(asfloat(r16.xy) * asfloat(r16.yy)); // 18741: mul
          r11.z = asuint(asfloat(r16.y) * asfloat(r16.z)); // 18748: mul
          r12.w = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r16.xyzw))); // 18755: dp4
          r12.w = asuint(asfloat(r12.w) + 1.0f); // 18765: add
          r13.x = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r16.xyzw))); // 18772: dp4
          r13.x = asuint(asfloat(r13.x) + 1.0f); // 18782: add
          r11.z = asuint(mad(asfloat(r11.z), 0.00257857703f, asfloat(r13.x))); // 18789: mad
          r10.z = asuint(asfloat(r10.z) * asfloat(r12.w)); // 18798: mul
          r10.z = asuint(asfloat(r10.z) / asfloat(r11.z)); // 18805: div
          r15.y = asuint(asfloat(r10.z) * asfloat(r10.w)); // 18812: mul
        } // 18819: endif
        r10.z = asuint(asfloat(r14.y) * asfloat(r1.x)); // 18820: mul
        r10.w = asuint(mad(asfloat(r11.x), asfloat(r14.x), -(asfloat(r10.z)))); // 18827: mad
        r10.w = asuint(asfloat(r10.w) * 1.7724539f); // 18837: mul
        r11.z = asuint(asfloat(r1.x) * asfloat(r1.x)); // 18844: mul
        r12.w = asuint(asfloat(r10.y) * asfloat(r11.z)); // 18851: mul
        r12.w = asuint(asfloat(r12.w) * 1.44269502f); // 18858: mul
        r12.w = asuint(exp2(asfloat(r12.w))); // 18865: exp
        r12.w = asuint(asfloat(r10.w) * asfloat(r12.w)); // 18870: mul
        r12.w = asuint(asfloat(r15.x) * asfloat(r12.w)); // 18877: mul
        r11.y = asuint(asfloat(r11.y) * asfloat(r11.y)); // 18884: mul
        r10.y = asuint(asfloat(r10.y) * asfloat(r11.y)); // 18891: mul
        r10.y = asuint(asfloat(r10.y) * 1.44269502f); // 18898: mul
        r10.y = asuint(exp2(asfloat(r10.y))); // 18905: exp
        r10.y = asuint(asfloat(r10.y) * asfloat(r10.w)); // 18910: mul
        r10.y = asuint(asfloat(r15.y) * asfloat(r10.y)); // 18917: mul
        r8.x = asuint(asfloat(r8.x) * asfloat(r9.z)); // 18924: mul
        r9.z = asuint(asfloat(r14.y) / asfloat(r11.x)); // 18931: div
        r9.y = (9.99999975e-06f < abs(asfloat(r9.y))) ? 0xffffffffu : 0u; // 18938: lt
        r10.w = asuint(-(asfloat(r8.w)) + 1.0f); // 18946: add
        r11.x = asuint(asfloat(r9.z) * asfloat(r10.w)); // 18954: mul
        r8.w = asuint(mad(asfloat(r8.w), asfloat(r10.y), -(asfloat(r12.w)))); // 18961: mad
        r8.x = asuint(asfloat(r8.w) / asfloat(r8.x)); // 18971: div
        r8.x = asuint(mad(asfloat(r9.z), asfloat(r10.w), asfloat(r8.x))); // 18978: mad
        r8.w = (asfloat(r8.w) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 18987: lt
        r9.z = (9.99999997e-07f < abs(asfloat(r1.x))) ? 0xffffffffu : 0u; // 18994: lt
        r10.y = asuint(mad(asfloat(r1.x), asfloat(r14.x), asfloat(r14.y))); // 19002: mad
        r10.z = asuint(mad(asfloat(r10.z), asfloat(r6.w), asfloat(r10.y))); // 19011: mad
        r1.x = asuint(asfloat(r6.w) * -(asfloat(r1.x))); // 19020: mul
        r1.x = asuint(asfloat(r1.x) * 1.44269502f); // 19028: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 19035: exp
        r1.x = asuint(mad(-(asfloat(r10.z)), asfloat(r1.x), asfloat(r10.y))); // 19040: mad
        r1.x = asuint(asfloat(r1.x) / asfloat(r11.z)); // 19050: div
        r1.x = (r9.z != 0u) ? r1.x : r15.z; // 19057: movc
        r1.x = (r8.w != 0u) ? r11.x : r1.x; // 19066: movc
        r1.x = (r9.y != 0u) ? r8.x : r1.x; // 19075: movc
        r1.x = asuint(asfloat(r7.w) * asfloat(r1.x)); // 19084: mul
        r7.w = (1.0f < asfloat(r1.x)) ? 0xffffffffu : 0u; // 19091: lt
        r1.x = (r7.w != 0u) ? 0x3f800000u : r1.x; // 19098: movc
        r1.x = r1.x  & r9.w; // 19107: and
        r9.yzw = asuint(asfloat(r1.xxx) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 19114: mul
        r1.x = asuint(max(asfloat(r5.x), 0.0f)); // 19122: max
        r5.x = asuint(mad(-(asfloat(r1.x)), 0.5f, asfloat(r11.w))); // 19129: mad
        r1.x = asuint(asfloat(r1.x) * asfloat(r5.x)); // 19139: mul
        r1.x = asuint(asfloat(r5.y) * asfloat(r1.x)); // 19146: mul
        r1.x = asuint(mad(asfloat(r0.z), asfloat(r1.x), asfloat(r5.w))); // 19153: mad
        r1.x = asuint(asfloat(r1.x) * -1.44269502f); // 19162: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 19169: exp
        r9.yzw = asuint(asfloat(r4.xxx) * asfloat(r9.yzw)); // 19174: mul
        r9.yzw = asuint(mad(asfloat(r12.xyz), asfloat(r1.xxx), asfloat(r9.yzw))); // 19181: mad
        r0.w = asuint(mad(asfloat(r0.x), asfloat(r1.z), asfloat(r0.w))); // 19190: mad
        r1.x = asuint(max(asfloat(r0.w), asfloat(r3.z))); // 19199: max
        r4.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 19206: add
        r4.x = asuint(asfloat(r4.x) / asfloat(r3.y)); // 19215: div
        r4.x = asuint(max(asfloat(r4.x), 0.0f)); // 19222: max
        r4.x = (r3.w != 0u) ? r4.x : 0x49742400u; // 19229: movc
        r5.x = asuint(mad(-(asfloat(r10.x)), asfloat(r6.w), asfloat(r8.z))); // 19238: mad
        r5.x = asuint(asfloat(r6.w) * asfloat(r5.x)); // 19248: mul
        r10.x = asuint(asfloat(r5.y) * asfloat(r5.x)); // 19255: mul
        r1.x = asuint(min(asfloat(r1.x), g_fog_height_top)); // 19262: min
        r1.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 19270: add
        r1.x = asuint(mad(-(asfloat(r5.z)), asfloat(r4.x), asfloat(r1.x))); // 19279: mad
        r1.x = asuint(asfloat(r4.x) * asfloat(r1.x)); // 19289: mul
        r10.y = asuint(asfloat(r5.y) * asfloat(r1.x)); // 19296: mul
        r5.xy = asuint(asfloat(r0.zz) * asfloat(r10.xy)); // 19303: mul
        r1.x = asuint(asfloat(r5.y) + asfloat(r5.x)); // 19310: add
        r4.x = asuint(asfloat(r8.y) + -(asfloat(r9.x))); // 19317: add
        r5.x = (r4.x & 0x7fffffffu); // 19325: mov
        r0.w = asuint(max(asfloat(r0.w), g_fog_height_bottom)); // 19331: max
        r0.w = asuint(-(asfloat(r0.w)) + asfloat(r3.z)); // 19339: add
        r0.w = asuint(asfloat(r0.w) / asfloat(r3.y)); // 19347: div
        r0.w = asuint(max(asfloat(r0.w), 0.0f)); // 19354: max
        r5.y = (r3.w != 0u) ? r0.w : 0x49742400u; // 19361: movc
        r5.xy = asuint(asfloat(r0.zz) * asfloat(r5.xy)); // 19370: mul
        r0.w = asuint(asfloat(r5.y) + asfloat(r5.x)); // 19377: add
        r0.w = asuint(asfloat(r0.w) + asfloat(r1.x)); // 19384: add
        r0.w = asuint(asfloat(r0.w) * -1.44269502f); // 19391: mul
        r0.w = asuint(exp2(asfloat(r0.w))); // 19398: exp
        r1.x = asuint(dot(-(asfloat(r1.yzw)), float3(sun_disk_direction.x, sun_disk_direction.y, sun_disk_direction.z))); // 19403: dp3
        r1.y = asuint(mad(asfloat(r1.x), asfloat(r1.x), 1.0f)); // 19412: mad
        r1.x = asuint(mad(-(asfloat(r1.x)), 1.51999998f, 1.5776f)); // 19421: mad
        r1.x = asuint(log2(abs(asfloat(r1.x)))); // 19431: log
        r1.xw = asuint(asfloat(r1.xy) * float2(1.5f, 0.0596831031f)); // 19437: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 19447: exp
        r1.x = asuint(asfloat(r1.y) / asfloat(r1.x)); // 19452: div
        r1.x = asuint(asfloat(r1.x) * g_sun_disc_color_scale); // 19459: mul
        r3.w = asuint(asfloat(r1.x) * 0.0195609424f); // 19467: mul
        r1.x = asuint(mad(asfloat(r1.x), 0.0195609424f, -(asfloat(r1.w)))); // 19474: mad
        r1.x = asuint(mad(g_height_fog_forward_scattering, asfloat(r1.x), asfloat(r1.w))); // 19484: mad
        r5.xy = asuint(float2(1.0f, 1.0f) / asfloat(r6.xy)); // 19494: div
        r5.zw = asuint(asfloat(r3.xx) * -(asfloat(r5.xy))); // 19504: mul
        r5.zw = asuint(asfloat(r5.zw) * float2(1.44269502f, 1.44269502f)); // 19512: mul
        r5.zw = asuint(exp2(asfloat(r5.zw))); // 19522: exp
        r6.xyw = asuint(asfloat(r5.zzz) * float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z)); // 19527: mul
        r8.xy = asuint(asfloat(r1.zz) * asfloat(r5.xy)); // 19535: mul
        r5.xy = asuint(asfloat(r3.yy) * asfloat(r5.xy)); // 19542: mul
        r8.zw = asuint(asfloat(r0.xx) * -(asfloat(r8.xy))); // 19549: mul
        r8.zw = asuint(asfloat(r8.zw) * float2(1.44269502f, 1.44269502f)); // 19557: mul
        r8.zw = asuint(exp2(asfloat(r8.zw))); // 19567: exp
        r10.xy = (float2(1.00000001e-10f, 1.00000001e-10f) < abs(asfloat(r8.xy))) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 19572: lt
        r11.xyz = asuint(asfloat(r6.xyw) / asfloat(r8.xxx)); // 19583: div
        r10.zw = asuint(-(asfloat(r8.zw)) + float2(1.0f, 1.0f)); // 19590: add
        r12.xyz = asuint(asfloat(r10.zzz) * asfloat(r11.xyz)); // 19601: mul
        r12.xyz = asuint(asfloat(r12.xyz) * float3(g_fog_density_constant, g_fog_density_constant, g_fog_density_constant)); // 19608: mul
        r14.xyz = asuint(asfloat(r6.xyw) * float3(g_fog_density_constant, g_fog_density_constant, g_fog_density_constant)); // 19616: mul
        r12.xyz = (r10.xxx != uint3(0u, 0u, 0u)) ? r12.xyz : r14.xyz; // 19624: movc
        r14.xyz = asuint(asfloat(r6.xyw) / asfloat(r5.xxx)); // 19633: div
        r14.xyz = asuint(mad(asfloat(r14.xyz), asfloat(r8.zzz), asfloat(r12.xyz))); // 19640: mad
        r0.x = asuint(asfloat(r5.w) * 2.20000002e-05f); // 19649: mul
        r1.w = asuint(asfloat(r0.x) / asfloat(r8.y)); // 19656: div
        r3.x = asuint(asfloat(r10.w) * asfloat(r1.w)); // 19663: mul
        r4.x = asuint(asfloat(r3.x) * g_fog_density_constant); // 19670: mul
        r5.z = asuint(asfloat(r0.x) * g_fog_density_constant); // 19678: mul
        r4.x = (r10.y != 0u) ? r4.x : r5.z; // 19686: movc
        r0.x = asuint(asfloat(r0.x) / asfloat(r5.y)); // 19695: div
        r5.y = asuint(mad(asfloat(r0.x), asfloat(r8.w), asfloat(r4.x))); // 19702: mad
        r5.yzw = asuint(-(asfloat(r5.yyy)) + -(asfloat(r14.xyz))); // 19711: add
        r5.yzw = asuint(asfloat(r5.yzw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 19720: mul
        r5.yzw = asuint(exp2(asfloat(r5.yzw))); // 19730: exp
        r8.xzw = asuint(-(asfloat(r4.xxx)) + -(asfloat(r12.xyz))); // 19735: add
        r8.xzw = asuint(asfloat(r8.xzw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 19744: mul
        r8.xzw = asuint(exp2(asfloat(r8.xzw))); // 19754: exp
        r8.xzw = asuint(-(asfloat(r8.xzw)) + float3(1.0f, 1.0f, 1.0f)); // 19759: add
        r4.x = asuint(asfloat(r3.y) * g_fog_density_constant); // 19770: mul
        r4.x = asuint(asfloat(r1.z) / asfloat(r4.x)); // 19778: div
        r4.x = asuint(-(asfloat(r4.x)) + 1.0f); // 19785: add
        r1.z = asuint(mad(-(g_fog_density_constant), asfloat(r3.y), asfloat(r1.z))); // 19793: mad
        r10.xyw = asuint(-(asfloat(r1.www)) + -(asfloat(r11.xyz))); // 19804: add
        r10.xyw = asuint(asfloat(r10.xyw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 19813: mul
        r10.xyw = asuint(exp2(asfloat(r10.xyw))); // 19823: exp
        r11.xyz = asuint(mad(asfloat(r11.xyz), asfloat(r10.zzz), asfloat(r3.xxx))); // 19828: mad
        r10.xyz = asuint(asfloat(r10.xyw) * asfloat(r11.xyz)); // 19837: mul
        r1.w = (1.00000002e-16f < asfloat(r8.y)) ? 0xffffffffu : 0u; // 19844: lt
        r11.xyz = asuint(-(asfloat(r5.yzw)) + float3(1.0f, 1.0f, 1.0f)); // 19851: add
        r10.xyz = (r1.www != uint3(0u, 0u, 0u)) ? r10.xyz : r11.xyz; // 19862: movc
        r1.z = (abs(asfloat(r1.z)) >= 9.99999975e-06f) ? 0xffffffffu : 0u; // 19871: ge
        r6.xyw = asuint(-(asfloat(r6.xyw)) / asfloat(r5.xxx)); // 19879: div
        r6.xyw = asuint(-(asfloat(r0.xxx)) + asfloat(r6.xyw)); // 19887: add
        r6.xyw = asuint(asfloat(r6.xyw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 19895: mul
        r6.xyw = asuint(exp2(asfloat(r6.xyw))); // 19905: exp
        r11.xyz = asuint(float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z) + float3(2.20000002e-05f, 2.20000002e-05f, 2.20000002e-05f)); // 19910: add
        r11.xyz = asuint(float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z) / asfloat(r11.xyz)); // 19921: div
        r12.xyz = (float3(1.00000001e-10f, 1.00000001e-10f, 1.00000001e-10f) < asfloat(r11.xyz)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 19929: lt
        r11.xyz = (r12.xyz != uint3(0u, 0u, 0u)) ? r11.xyz : uint3(0x3f000000u, 0x3f000000u, 0x3f000000u); // 19939: movc
        r0.x = asuint(mad(asfloat(r1.y), 0.0596831031f, -(asfloat(r3.w)))); // 19951: mad
        r3.xyw = asuint(mad(asfloat(r11.xyz), asfloat(r0.xxx), asfloat(r3.www))); // 19961: mad
        r6.xyw = asuint(-(asfloat(r5.yzw)) + asfloat(r6.xyw)); // 19970: add
        r6.xyw = asuint(asfloat(r6.xyw) / asfloat(r4.xxx)); // 19978: div
        r1.yzw = (r1.zzz != uint3(0u, 0u, 0u)) ? r6.xyw : r10.xyz; // 19985: movc
        r1.yzw = asuint(asfloat(r1.yzw) * float3(g_fog_colour.x, g_fog_colour.y, g_fog_colour.z)); // 19994: mul
        r1.yzw = asuint(asfloat(r4.www) * asfloat(r1.yzw)); // 20002: mul
        r6.xyw = asuint(asfloat(r4.www) * asfloat(r5.yzw)); // 20009: mul
        r10.xyz = asuint(asfloat(r7.xyz) * asfloat(r1.xxx)); // 20016: mul
        r9.xyz = asuint(asfloat(r9.yzw) * float3(ambient_cube_tb[0].x, ambient_cube_tb[0].y, ambient_cube_tb[0].z)); // 20023: mul
        r9.xyz = asuint(asfloat(r9.xyz) * float3(0.0795774683f, 0.0795774683f, 0.0795774683f)); // 20031: mul
        r9.xyz = asuint(mad(asfloat(r10.xyz), asfloat(r13.yzw), asfloat(r9.xyz))); // 20041: mad
        r0.x = asuint(mad(-(asfloat(r5.y)), asfloat(r4.w), 1.0f)); // 20050: mad
        r1.x = asuint(-(asfloat(r0.w)) + asfloat(r0.x)); // 20060: add
        r1.x = asuint(asfloat(r1.x) + 1.0f); // 20068: add
        r4.x = (1.00000001e-10f < asfloat(r1.x)) ? 0xffffffffu : 0u; // 20075: lt
        r0.x = asuint(asfloat(r0.x) / asfloat(r1.x)); // 20082: div
        r0.x = (r4.x != 0u) ? r0.x : 0x3f800000u; // 20089: movc
        r5.xyz = asuint(mad(asfloat(r5.yzw), asfloat(r4.www), float3(-1.0f, -1.0f, -1.0f))); // 20098: mad
        r5.xyz = asuint(mad(asfloat(r0.xxx), asfloat(r5.xyz), float3(1.0f, 1.0f, 1.0f))); // 20110: mad
        r1.x = (0.0f < g_fog_density_height) ? 0xffffffffu : 0u; // 20122: lt
        r1.x = (r1.x != 0u) ? r3.z : asuint(g_fog_height_top); // 20130: movc
        r3.z = asuint(max(camera_position.y, g_fog_height_bottom)); // 20140: max
        r3.z = asuint(asfloat(r1.x) + -(asfloat(r3.z))); // 20149: add
        r3.z = asuint(max(asfloat(r3.z), 0.0f)); // 20157: max
        r4.x = asuint(max(asfloat(r1.x), camera_position.y)); // 20164: max
        r4.w = asuint(-(asfloat(r4.x)) + g_fog_height_top); // 20172: add
        r4.w = asuint(max(asfloat(r4.w), 0.0f)); // 20181: max
        r1.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 20188: add
        r5.w = (0.0f != asfloat(r1.x)) ? 0xffffffffu : 0u; // 20197: ne
        r1.x = asuint(1.0f / asfloat(r1.x)); // 20207: div
        r1.x = r1.x  & r5.w; // 20217: and
        r4.x = asuint(min(asfloat(r4.x), g_fog_height_top)); // 20224: min
        r4.x = asuint(-(asfloat(r4.x)) + g_fog_height_top); // 20232: add
        r4.x = asuint(mad(-(asfloat(r4.w)), 0.5f, asfloat(r4.x))); // 20241: mad
        r4.x = asuint(asfloat(r4.w) * asfloat(r4.x)); // 20251: mul
        r1.x = asuint(asfloat(r1.x) * asfloat(r4.x)); // 20258: mul
        r1.x = asuint(asfloat(r0.z) * asfloat(r1.x)); // 20265: mul
        r0.z = asuint(mad(-(asfloat(r0.z)), asfloat(r3.z), -(asfloat(r1.x)))); // 20272: mad
        r0.z = asuint(asfloat(r0.z) * 1.44269502f); // 20283: mul
        r0.z = asuint(exp2(asfloat(r0.z))); // 20290: exp
        r4.xzw = asuint(-(asfloat(r4.zzz)) + float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z)); // 20295: add
        r4.xzw = asuint(asfloat(r6.zzz) * asfloat(r4.xzw)); // 20304: mul
        r10.xyz = asuint(asfloat(r0.zzz) * float3(ambient_cube_tb[0].x, ambient_cube_tb[0].y, ambient_cube_tb[0].z)); // 20311: mul
        r8.xyz = asuint(asfloat(r8.xzw) * asfloat(r10.xyz)); // 20319: mul
        r3.xyz = asuint(asfloat(r3.xyw) * asfloat(r7.xyz)); // 20326: mul
        r1.xyz = asuint(asfloat(r1.yzw) * asfloat(r3.xyz)); // 20333: mul
        r1.xyz = asuint(mad(asfloat(r8.xyz), float3(g_fog_colour.x, g_fog_colour.y, g_fog_colour.z), asfloat(r1.xyz))); // 20340: mad
        r0.z = asuint(1.0f / asfloat(r4.y)); // 20350: div
        r0.z = asuint(-(asfloat(r0.w)) + asfloat(r0.z)); // 20360: add
        r0.x = asuint(mad(asfloat(r0.x), asfloat(r0.z), asfloat(r0.w))); // 20368: mad
        r1.xyz = asuint(asfloat(r0.xxx) * asfloat(r1.xyz)); // 20377: mul
        r0.xzw = asuint(asfloat(r0.www) * asfloat(r6.xyw)); // 20384: mul
        r0.xzw = asuint(asfloat(r4.yyy) * asfloat(r0.xzw)); // 20391: mul
        r1.xyz = asuint(mad(asfloat(r9.xyz), asfloat(r5.xyz), asfloat(r1.xyz))); // 20398: mad
        r3.xyz = asuint(asfloat(r4.xzw) * float3(-1.44269502f, -1.44269502f, -1.44269502f)); // 20407: mul
        r3.xyz = asuint(exp2(asfloat(r3.xyz))); // 20417: exp
        r1.xyz = asuint(asfloat(r1.xyz) * asfloat(r3.xyz)); // 20422: mul
        r1.xyz = asuint(asfloat(r4.yyy) * asfloat(r1.xyz)); // 20429: mul
        r1.w = r1.x  & 0x7fffffffu; // 20436: and
        r1.w = (r1.w >= 0x7f800000u) ? 0xffffffffu : 0u; // 20443: uge
        r1.xyz = (r1.www != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : r1.xyz; // 20450: movc
        r0.xzw = (r1.www != uint3(0u, 0u, 0u)) ? uint3(0x3f800000u, 0x3f800000u, 0x3f800000u) : r0.xzw; // 20462: movc
        r1.xyz = asuint(asfloat(r2.www) * asfloat(r1.xyz)); // 20474: mul
        r2.xyz = asuint(mad(asfloat(r2.xyz), asfloat(r0.xzw), asfloat(r1.xyz))); // 20481: mad
      } // 20490: endif
    } // 20491: endif
  } // 20492: endif
  r2.xyz = asuint(asfloat(r0.yyy) * asfloat(r2.xyz)); // 20493: mul
  o0.xyzw = asfloat(r2.xyzw); // 20500: mov
  return; // 20505: ret
}
