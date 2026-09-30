// Reconstructed from the supplied 0xE8524BB1.ps_5_0.cso (Low/Medium/High VFX).
// Entry: main, target: ps_5_0. Compilation/game testing is still required.
// Temporary registers hold uint bits, matching DXBC's untyped registers.
// asfloat/asint reinterpret bits; explicit casts only perform real conversions.
// Original named buffer layouts, resource slots, sampling and flow are retained.
// Trailing numbers identify original SHEX DWORD offsets for inspection.
//
// HDR rendering uses VfxBaseBrightness from shared.h; SDR uses 1.0.
// Brightness is applied to particle RGB after lighting and before fog.
// Do not multiply the final RGBA output: alpha and added fog must stay intact.

#include "../shared.h"

// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 22 10:26:14 2026

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
SamplerState g_sam_diffuse_s : register(s1);
SamplerState g_sam_normal_s : register(s2);
TextureCube<float4> t_sky : register(t0);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t1);
Texture2D<float4> gbuffer_channel_4_texture : register(t2);
Texture2DArray<float4> g_tex_diffuse : register(t3);
Texture2DArray<float4> g_tex_normal : register(t4);
StructuredBuffer<EMITTER_CONSTANTS_PS> g_constant_buffer_ps : register(t5);


// HDR and VfxBaseBrightness are supplied by ../shared.h.
// Values are LINEAR multipliers: 1.0 = neutral, 2.0 = twice the particle RGB.
// emitterIndex is available if your own classification selects several sliders.
float PharaohVfxBrightness(uint emitterIndex)
{
  return HDR >= 0.5f ? VfxBaseBrightness : 1.0f;
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

  r0.xyzw = uint4(g_constant_buffer_ps[v2.x].m_behaviour_mask, g_constant_buffer_ps[v2.x].m_diffuse_id, g_constant_buffer_ps[v2.x].m_normal_id, g_constant_buffer_ps[v2.x].m_draw_flags); // 96: ld_structured
  r1.xyzw = uint4(asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_small_particles), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_large_particles), g_constant_buffer_ps[v2.x].m_ps_alpha_crush_sharp_transition, asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_alpha_crush_min)); // 107: ld_structured
  r2.xyzw = uint4(asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_alpha_crush_max), asuint(g_constant_buffer_ps[v2.x].m_ps_alpha_crush_variation), asuint(g_constant_buffer_ps[v2.x].m_ps_soft_edge_soft_factor), g_constant_buffer_ps[v2.x].m_ps_texture_overlay_colour); // 118: ld_structured
  r3.xyzw = r0.xxxx  & uint4(0x00010000u, 0x00100000u, 0x00200000u, 0x00020000u); // 129: and
  if (r3.x != 0u) { // 139: if
    r4.z = asuint((float)(r0.y)); // 142: utof
    r4.xy = asuint(v3.xy); // 147: mov
    r5.xyzw = asuint(g_tex_diffuse.Sample(g_sam_diffuse_s, asfloat(r4.xyz)).xyzw); // 152: sample
    r4.xy = asuint(v5.xy); // 163: mov
    r4.xyzw = asuint(g_tex_diffuse.Sample(g_sam_diffuse_s, asfloat(r4.xyz)).xyzw); // 168: sample
    r4.xyzw = asuint(-(asfloat(r5.xyzw)) + asfloat(r4.xyzw)); // 179: add
    r4.xyzw = asuint(mad(v5.zzzz, asfloat(r4.xyzw), asfloat(r5.xyzw))); // 187: mad
    r0.y = (0.0404499993f >= asfloat(r4.w)) ? 0xffffffffu : 0u; // 196: ge
    r3.x = asuint(asfloat(r4.w) * 0.0773993805f); // 203: mul
    r4.w = asuint(asfloat(r4.w) + 0.0549999997f); // 210: add
    r4.w = asuint(asfloat(r4.w) * 0.947867334f); // 217: mul
    r4.w = asuint(log2(asfloat(r4.w))); // 224: log
    r4.w = asuint(asfloat(r4.w) * 2.4000001f); // 229: mul
    r4.w = asuint(exp2(asfloat(r4.w))); // 236: exp
    r0.y = (r0.y != 0u) ? r3.x : r4.w; // 241: movc
    r5.xyz = asuint(min(asfloat(r0.yyy), asfloat(r4.xyz))); // 250: min
    r3.x = asuint(dot(v1.xyz, float3(0.212599993f, 0.715200007f, 0.0722000003f))); // 257: dp3
    r6.xyz = asuint(v1.xyz / asfloat(r3.xxx)); // 267: div
    r4.w = (asfloat(r0.y) < 0.5f) ? 0xffffffffu : 0u; // 274: lt
    r7.xyz = asuint(asfloat(r5.xyz) * asfloat(r6.xyz)); // 281: mul
    r7.xyz = asuint(asfloat(r7.xyz) + asfloat(r7.xyz)); // 288: add
    r8.xyz = asuint(-(asfloat(r5.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 295: add
    r8.xyz = asuint(asfloat(r8.xyz) + asfloat(r8.xyz)); // 306: add
    r9.xyz = asuint(-(asfloat(r6.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 313: add
    r8.xyz = asuint(mad(-(asfloat(r8.xyz)), asfloat(r9.xyz), float3(1.0f, 1.0f, 1.0f))); // 324: mad
    r5.w = r4.w  & 0x3f800000u; // 337: and
    r4.w = (r4.w != 0u) ? 0x00000000u : 0x3f800000u; // 344: movc
    r8.xyz = asuint(asfloat(r8.xyz) * asfloat(r4.www)); // 353: mul
    r7.xyz = asuint(mad(asfloat(r5.www), asfloat(r7.xyz), asfloat(r8.xyz))); // 360: mad
    r7.xyz = asuint(asfloat(r3.xxx) * asfloat(r7.xyz)); // 369: mul
    r5.xyz = asuint(asfloat(r5.xyz) * v1.xyz); // 376: mul
    r5.xyz = (r2.www != uint3(0u, 0u, 0u)) ? r7.xyz : r5.xyz; // 383: movc
    r5.xyz = asuint(asfloat(r5.xyz) * v1.www); // 392: mul
    r5.w = asuint(saturate(asfloat(r0.y) * v1.w)); // 399: mul
    r4.xyz = asuint(saturate(-(asfloat(r0.yyy)) + asfloat(r4.xyz))); // 406: add
    r7.xyz = (asfloat(r4.xyz) < float3(0.5f, 0.5f, 0.5f)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 414: lt
    r6.xyz = asuint(asfloat(r4.xyz) * asfloat(r6.xyz)); // 424: mul
    r6.xyz = asuint(asfloat(r6.xyz) + asfloat(r6.xyz)); // 431: add
    r8.xyz = asuint(-(asfloat(r4.xyz)) + float3(1.0f, 1.0f, 1.0f)); // 438: add
    r8.xyz = asuint(asfloat(r8.xyz) * asfloat(r9.xyz)); // 449: mul
    r8.xyz = asuint(mad(-(asfloat(r8.xyz)), float3(2.0f, 2.0f, 2.0f), float3(1.0f, 1.0f, 1.0f))); // 456: mad
    r9.xyz = r7.xyz  & uint3(0x3f800000u, 0x3f800000u, 0x3f800000u); // 472: and
    r7.xyz = (r7.xyz != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : uint3(0x3f800000u, 0x3f800000u, 0x3f800000u); // 482: movc
    r7.xyz = asuint(asfloat(r8.xyz) * asfloat(r7.xyz)); // 497: mul
    r6.xyz = asuint(mad(asfloat(r9.xyz), asfloat(r6.xyz), asfloat(r7.xyz))); // 504: mad
    r6.xyz = asuint(asfloat(r3.xxx) * asfloat(r6.xyz)); // 513: mul
    r4.xyz = asuint(asfloat(r4.xyz) * v1.xyz); // 520: mul
    r4.xyz = (r2.www != uint3(0u, 0u, 0u)) ? r6.xyz : r4.xyz; // 527: movc
    r4.xyz = asuint(asfloat(r4.xyz) * v1.www); // 536: mul
  } else { // 543: else
    r5.xyzw = asuint(v1.xyzw); // 544: mov
    r4.xyz = uint3(0x00000000u, 0x00000000u, 0x00000000u); // 549: mov
  } // 557: endif
  r0.y = (asfloat(r1.w) == asfloat(r2.x)) ? 0xffffffffu : 0u; // 558: eq
  r2.y = asuint(-(asfloat(r2.y)) + 1.0f); // 565: add
  r2.w = asuint(max(asfloat(r1.w), asfloat(r2.y))); // 573: max
  r6.xy = asuint(v4.ww * float2(3276.0f, 4099.0f)); // 580: mul
  r6.xy = asuint(frac(asfloat(r6.xy))); // 590: frc
  r2.w = asuint(asfloat(r2.w) + -1.0f); // 595: add
  r2.w = asuint(mad(asfloat(r6.x), asfloat(r2.w), 1.0f)); // 602: mad
  r2.y = asuint(asfloat(r2.y) * asfloat(r2.x)); // 611: mul
  r2.y = asuint(max(asfloat(r1.w), asfloat(r2.y))); // 618: max
  r2.y = asuint(-(asfloat(r2.x)) + asfloat(r2.y)); // 625: add
  r2.x = asuint(mad(asfloat(r6.y), asfloat(r2.y), asfloat(r2.x))); // 633: mad
  r0.y = (r0.y != 0u) ? r2.w : r2.x; // 642: movc
  r0.y = asuint(-(asfloat(r1.w)) + asfloat(r0.y)); // 651: add
  r0.y = asuint(max(asfloat(r0.y), 0.00100000005f)); // 659: max
  r1.w = asuint(-(asfloat(r1.w)) + v6.w); // 666: add
  r0.y = asuint(saturate(asfloat(r1.w) / asfloat(r0.y))); // 674: div
  r1.w = asuint(saturate(-(asfloat(r0.y)) + asfloat(r5.w))); // 681: add
  r2.x = asuint(-(asfloat(r0.y)) + 1.00100005f); // 689: add
  r1.w = asuint(asfloat(r1.w) / asfloat(r2.x)); // 697: div
  r0.y = asuint(-(asfloat(r0.y)) + asfloat(r1.w)); // 704: add
  r0.y = (asfloat(r0.y) >= 0.0f) ? 0xffffffffu : 0u; // 712: ge
  r0.xy = r0.xy  & uint2(0x00080000u, 0x3f800000u); // 719: and
  r0.y = asuint(asfloat(r1.w) * asfloat(r0.y)); // 729: mul
  r6.w = (r1.z != 0u) ? r0.y : r1.w; // 736: movc
  r0.y = (asfloat(r5.w) != 0.0f) ? 0xffffffffu : 0u; // 745: ne
  r1.z = asuint(asfloat(r6.w) / asfloat(r5.w)); // 752: div
  r0.y = (r0.y != 0u) ? r1.z : 0x3f800000u; // 759: movc
  r6.xyz = asuint(asfloat(r0.yyy) * asfloat(r5.xyz)); // 768: mul
  r5.xyzw = (r3.yyyy != uint4(0u, 0u, 0u, 0u)) ? r6.xyzw : r5.xyzw; // 775: movc
  if (r3.z != 0u) { // 784: if
    r6.xyz = asuint(v6.xyz); // 787: mov
    r6.w = 0x3f800000u; // 792: mov
    r0.y = asuint(dot(asfloat(r6.xyzw), float4(view._m02, view._m12, view._m22, view._m32))); // 797: dp4
    r1.zw = asuint(v0.xy + float2(g_vpos_texel_offset, g_vpos_texel_offset)); // 805: add
    r1.zw = asuint(asfloat(r1.zw) * float2(g_render_target_dimensions.z, g_render_target_dimensions.w)); // 813: mul
    r2.x = (1 < g_num_of_samples) ? 0xffffffffu : 0u; // 821: ilt
    if (r2.x != 0u) { // 829: if
      uint4 dimensions_832 = uint4(0u, 0u, 0u, 0u);
      gbuffer_channel_4_texture_ms.GetDimensions(dimensions_832.x, dimensions_832.y, dimensions_832.w);
      r2.yw = dimensions_832.xy; // 832: resinfo
    } else { // 841: else
      uint4 dimensions_842 = uint4(0u, 0u, 0u, 0u);
      gbuffer_channel_4_texture.GetDimensions(0x00000000u, dimensions_842.x, dimensions_842.y, dimensions_842.w);
      r2.yw = dimensions_842.xy; // 842: resinfo
    } // 851: endif
    r3.xy = asuint((float2)(r2.yw)); // 852: utof
    r3.xy = asuint(asfloat(r1.zw) * asfloat(r3.xy)); // 857: mul
    r3.xy = asuint(floor(asfloat(r3.xy))); // 864: round_ni
    r3.xy = asuint((int2)(asfloat(r3.xy))); // 869: ftoi
    r2.yw = asuint(asint(r2.yw) + int2(-1, -1)); // 874: iadd
    r3.xy = asuint(max(asint(r3.xy), int2(0, 0))); // 884: imax
    r6.xy = asuint(min(asint(r2.yw), asint(r3.xy))); // 894: imin
    if (r2.x != 0u) { // 901: if
      r6.z = 0x00000000u; // 904: mov
      r7.z = asuint(gbuffer_channel_4_texture_ms.Load(asint(r6.xy), 0).x); // 909: ld_ms
    } else { // 920: else
      r6.w = 0x00000000u; // 921: mov
      r7.z = asuint(gbuffer_channel_4_texture.Load(asint(r6.xyw)).x); // 926: ld
    } // 935: endif
    r7.xy = asuint(mad(asfloat(r1.zw), float2(2.0f, -2.0f), float2(-1.0f, 1.0f))); // 936: mad
    r7.w = 0x3f800000u; // 951: mov
    r1.z = asuint(dot(asfloat(r7.xyzw), float4(inv_projection._m02, inv_projection._m12, inv_projection._m22, inv_projection._m32))); // 956: dp4
    r1.w = asuint(dot(asfloat(r7.xyzw), float4(inv_projection._m03, inv_projection._m13, inv_projection._m23, inv_projection._m33))); // 964: dp4
    r1.z = asuint(asfloat(r1.z) / asfloat(r1.w)); // 972: div
    r0.y = asuint(-(asfloat(r0.y)) + asfloat(r1.z)); // 979: add
    r0.y = asuint(saturate(asfloat(r0.y) / asfloat(r2.z))); // 987: div
    r5.w = asuint(asfloat(r0.y) * asfloat(r5.w)); // 994: mul
  } else { // 1001: else
    r0.y = 0x3f800000u; // 1002: mov
  } // 1007: endif
  if (r3.w != 0u) { // 1008: if
    r2.z = asuint((float)(r0.z)); // 1011: utof
    r2.xy = asuint(v3.xy); // 1016: mov
    r2.xyz = asuint(g_tex_normal.Sample(g_sam_normal_s, asfloat(r2.xyz)).xyz); // 1021: sample
    r2.xyz = asuint(mad(asfloat(r2.xyz), float3(2.0f, 2.0f, 2.0f), float3(-1.0f, -1.0f, -1.0f))); // 1032: mad
    r0.z = asuint(dot(asfloat(r2.xyz), asfloat(r2.xyz))); // 1047: dp3
    r0.z = asuint(rsqrt(asfloat(r0.z))); // 1054: rsq
    r2.xyz = asuint(asfloat(r0.zzz) * asfloat(r2.xyz)); // 1059: mul
  } else { // 1066: else
    r2.xyz = uint3(0x00000000u, 0x00000000u, 0x3f800000u); // 1067: mov
  } // 1075: endif
  if (r0.x != 0u) { // 1076: if
    r3.xyzw = uint4(asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_emissive), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_directionality), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_average_size), asuint(g_constant_buffer_ps[v2.x].m_ps_lighting_additive_blend_mul)); // 1079: ld_structured
    r6.xyz = asuint(-(asfloat(r2.yyy)) * v8.xyz); // 1090: mul
    r2.xyw = asuint(mad(asfloat(r2.xxx), v7.xyz, asfloat(r6.xyz))); // 1098: mad
    r2.xyz = asuint(mad(asfloat(r2.zzz), v9.xyz, asfloat(r2.xyw))); // 1107: mad
    r2.w = (r2.z ^ 0x80000000u); // 1116: mov
    r6.x = asuint(dot(asfloat(r2.xyw), float3(inv_view._m00, inv_view._m10, inv_view._m20))); // 1122: dp3
    r6.y = asuint(dot(asfloat(r2.xyw), float3(inv_view._m01, inv_view._m11, inv_view._m21))); // 1130: dp3
    r6.z = asuint(dot(asfloat(r2.xyw), float3(inv_view._m02, inv_view._m12, inv_view._m22))); // 1138: dp3
    r0.x = r0.w  & 0x20000000u; // 1146: and
    r2.xyz = (r0.xxx != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x3f800000u, 0x00000000u) : r6.xyz; // 1153: movc
    r7.xyz = (asfloat(r2.xyz) < float3(0.0f, 0.0f, 0.0f)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 1165: lt
    r8.xyz = (r7.xxx != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_lr[1].x), asuint(ambient_cube_lr[1].y), asuint(ambient_cube_lr[1].z)) : uint3(asuint(ambient_cube_lr[0].x), asuint(ambient_cube_lr[0].y), asuint(ambient_cube_lr[0].z)); // 1175: movc
    r7.xyw = (r7.yyy != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_tb[1].x), asuint(ambient_cube_tb[1].y), asuint(ambient_cube_tb[1].z)) : uint3(asuint(ambient_cube_tb[0].x), asuint(ambient_cube_tb[0].y), asuint(ambient_cube_tb[0].z)); // 1186: movc
    r9.xyz = (r7.zzz != uint3(0u, 0u, 0u)) ? uint3(asuint(ambient_cube_fb[1].x), asuint(ambient_cube_fb[1].y), asuint(ambient_cube_fb[1].z)) : uint3(asuint(ambient_cube_fb[0].x), asuint(ambient_cube_fb[0].y), asuint(ambient_cube_fb[0].z)); // 1197: movc
    r2.xyz = asuint(asfloat(r2.xyz) * asfloat(r2.xyz)); // 1208: mul
    r7.xyz = asuint(asfloat(r7.xyw) * asfloat(r2.yyy)); // 1215: mul
    r2.xyw = asuint(mad(asfloat(r2.xxx), asfloat(r8.xyz), asfloat(r7.xyz))); // 1222: mad
    r2.xyz = asuint(mad(asfloat(r2.zzz), asfloat(r9.xyz), asfloat(r2.xyw))); // 1231: mad
    r0.z = asuint(saturate(asfloat(r3.x))); // 1240: mov
    r7.xyz = asuint(asfloat(r3.www) * asfloat(r4.xyz)); // 1245: mul
    if (r0.x == 0u) { // 1252: if
      r1.z = g_constant_buffer_ps[v2.x].m_ps_legacy_lighting; // 1255: ld_structured
      r8.x = asuint(inv_view._m20); // 1266: mov
      r8.y = asuint(inv_view._m21); // 1272: mov
      r8.z = asuint(inv_view._m22); // 1278: mov
      r1.w = asuint(dot(float3(sun_direction.x, sun_direction.y, sun_direction.z), asfloat(r8.xyz))); // 1284: dp3
      r2.w = asuint(asfloat(r1.y) + asfloat(r1.x)); // 1292: add
      r4.w = asuint(dot(-(float3(sun_direction.x, sun_direction.y, sun_direction.z)), asfloat(r6.xyz))); // 1299: dp3
      r6.x = asuint(asfloat(r4.w) * 0.699999988f); // 1308: mul
      r4.w = asuint(mad(asfloat(r4.w), -0.5f, 0.5f)); // 1315: mad
      r6.x = asuint(saturate(asfloat(r6.x))); // 1324: mov
      r4.w = (r1.z != 0u) ? r4.w : r6.x; // 1329: movc
      r4.w = asuint(-(asfloat(r2.w)) * asfloat(r4.w)); // 1338: mul
      r3.z = asuint(asfloat(r3.z) * asfloat(r4.w)); // 1346: mul
      r3.z = asuint(asfloat(r3.z) * v4.x); // 1353: mul
      r3.z = asuint(asfloat(r3.z) * 1.44269502f); // 1360: mul
      r3.z = asuint(exp2(asfloat(r3.z))); // 1367: exp
      r4.w = asuint(-(asfloat(r3.y)) + 1.0f); // 1372: add
      r6.x = asuint(mad(asfloat(r3.y), asfloat(r3.y), 1.0f)); // 1380: mad
      r3.y = asuint(dot(asfloat(r1.ww), asfloat(r3.yy))); // 1389: dp2
      r3.y = asuint(-(asfloat(r3.y)) + asfloat(r6.x)); // 1396: add
      r3.y = asuint(max(asfloat(r3.y), 0.0f)); // 1404: max
      r4.w = asuint(asfloat(r4.w) * asfloat(r4.w)); // 1411: mul
      r4.w = asuint(asfloat(r4.w) * 0.0795774683f); // 1418: mul
      r3.y = asuint(log2(asfloat(r3.y))); // 1425: log
      r3.y = asuint(asfloat(r3.y) * 1.5f); // 1430: mul
      r3.y = asuint(exp2(asfloat(r3.y))); // 1437: exp
      r3.y = asuint(asfloat(r4.w) / asfloat(r3.y)); // 1442: div
      r1.y = asuint(asfloat(r1.y) * asfloat(r3.y)); // 1449: mul
      r1.w = asuint(mad(asfloat(r1.w), asfloat(r1.w), 1.0f)); // 1456: mad
      r1.x = asuint(asfloat(r1.w) * asfloat(r1.x)); // 1465: mul
      r1.x = asuint(mad(asfloat(r1.x), 0.0596831031f, asfloat(r1.y))); // 1472: mad
      r1.x = asuint(asfloat(r1.x) / asfloat(r2.w)); // 1481: div
      r1.y = asuint(-(asfloat(r3.z)) + 1.0f); // 1488: add
      r1.w = asuint(asfloat(r1.y) * asfloat(r1.x)); // 1496: mul
      r1.x = asuint(mad(asfloat(r1.x), asfloat(r1.y), asfloat(r3.z))); // 1503: mad
      r1.x = (r1.z != 0u) ? r1.x : r1.w; // 1512: movc
      r1.xyz = asuint(asfloat(r1.xxx) * float3(sun_colour.x, sun_colour.y, sun_colour.z)); // 1521: mul
      r6.xyz = asuint(asfloat(r1.xyz) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 1529: mul
      r1.xyz = asuint(mad(-(asfloat(r1.xyz)), float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f), asfloat(r3.xxx))); // 1539: mad
      r1.xyz = asuint(mad(asfloat(r0.zzz), asfloat(r1.xyz), asfloat(r6.xyz))); // 1552: mad
      r6.xyz = asuint(asfloat(r5.xyz) * asfloat(r2.xyz)); // 1561: mul
      r1.xyz = asuint(mad(asfloat(r1.xyz), asfloat(r5.xyz), asfloat(r6.xyz))); // 1568: mad
      r5.xyz = asuint(mad(asfloat(r4.xyz), asfloat(r3.www), asfloat(r1.xyz))); // 1577: mad
    } else { // 1586: else
      r1.xyz = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(3.80762103e-05f, 3.80762103e-05f, 3.80762103e-05f)); // 1587: mul
      r1.xyz = (r0.xxx != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : r1.xyz; // 1598: movc
      r1.xyz = asuint(asfloat(r2.xyz) + asfloat(r1.xyz)); // 1610: add
      r2.xyz = asuint(-(asfloat(r1.xyz)) + asfloat(r3.xxx)); // 1617: add
      r1.xyz = asuint(mad(asfloat(r0.zzz), asfloat(r2.xyz), asfloat(r1.xyz))); // 1625: mad
      r5.xyz = asuint(mad(asfloat(r1.xyz), asfloat(r5.xyz), asfloat(r7.xyz))); // 1634: mad
    } // 1643: endif
  } // 1644: endif
  // VFX BRIGHTNESS HOOK: after particle lighting, before all fog paths.
  // Multiply RGB only; retain particle alpha and the later soft-edge fade.
  r5.xyz = asuint(asfloat(r5.xyz) * PharaohVfxBrightness(v2.x));

  r0.x = r0.w  & asuint(g_draw_flags.x); // 1645: and
  r0.x = r0.x  & 0x40000000u; // 1653: and
  if (r0.x != 0u) { // 1660: if
    if (g_fog_mode == 0u) { // 1663: if
      r1.xyz = asuint(v6.xyz + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 1667: add
      r0.x = asuint(dot(asfloat(r1.xyz), asfloat(r1.xyz))); // 1676: dp3
      r0.x = asuint(sqrt(asfloat(r0.x))); // 1683: sqrt
      r0.z = asuint(saturate(g_legacy_fog_distance_start)); // 1688: mov
      r0.z = asuint(-(asfloat(r0.z)) + 1.0f); // 1694: add
      r0.z = asuint(mad(asfloat(r0.z), 8.0f, -4.0f)); // 1702: mad
      r0.w = asuint(saturate(g_legacy_fog_distance_strength)); // 1711: mov
      r0.w = asuint(-(asfloat(r0.w)) + 1.0f); // 1717: add
      r0.w = asuint(asfloat(r0.w) * 1000.0f); // 1725: mul
      r0.w = asuint(asfloat(r0.x) / asfloat(r0.w)); // 1732: div
      r0.z = asuint(asfloat(r0.w) + asfloat(r0.z)); // 1739: add
      r0.z = asuint(asfloat(r0.z) * 1.44269502f); // 1746: mul
      r0.z = asuint(exp2(asfloat(r0.z))); // 1753: exp
      r0.z = asuint(g_legacy_fog_distance_scale / asfloat(r0.z)); // 1758: div
      r0.z = asuint(saturate(-(asfloat(r0.z)) + g_legacy_fog_distance_scale)); // 1766: add
      r0.w = asuint(-(v6.y) + g_legacy_fog_height_top); // 1775: add
      r2.x = asuint(-(g_legacy_fog_height_bottom) + g_legacy_fog_height_top); // 1784: add
      r0.w = asuint(asfloat(r0.w) + -(g_legacy_fog_height_bottom)); // 1794: add
      r2.x = asuint(1.0f / asfloat(r2.x)); // 1803: div
      r0.w = asuint(saturate(asfloat(r0.w) * asfloat(r2.x))); // 1813: mul
      r2.x = asuint(mad(asfloat(r0.w), -2.0f, 3.0f)); // 1820: mad
      r0.w = asuint(asfloat(r0.w) * asfloat(r0.w)); // 1829: mul
      r0.w = asuint(asfloat(r0.w) * asfloat(r2.x)); // 1836: mul
      r2.x = asuint(dot(asfloat(r1.xz), asfloat(r1.xz))); // 1843: dp2
      r2.x = asuint(sqrt(asfloat(r2.x))); // 1850: sqrt
      r2.y = asuint(max(g_legacy_fog_clear_distance, 0.00100000005f)); // 1855: max
      r2.y = asuint(1.0f / asfloat(r2.y)); // 1863: div
      r2.x = asuint(asfloat(r2.y) * asfloat(r2.x)); // 1873: mul
      r2.x = asuint(min(asfloat(r2.x), 1.0f)); // 1880: min
      r2.y = asuint(mad(asfloat(r2.x), -2.0f, 3.0f)); // 1887: mad
      r2.x = asuint(asfloat(r2.x) * asfloat(r2.x)); // 1896: mul
      r2.x = asuint(asfloat(r2.x) * asfloat(r2.y)); // 1903: mul
      r0.z = asuint(mad(g_legacy_fog_height_strength, asfloat(r0.w), asfloat(r0.z))); // 1910: mad
      r2.yzw = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(g_legacy_volume_fog_colour.x, g_legacy_volume_fog_colour.y, g_legacy_volume_fog_colour.z)); // 1920: mul
      r2.yzw = asuint(asfloat(r2.yzw) * float3(1.5f, 1.5f, 1.5f)); // 1929: mul
      r2.yzw = asuint(asfloat(r2.yzw) * abs(float3(sun_direction.y, sun_direction.y, sun_direction.y))); // 1939: mul
      r3.xyz = asuint(asfloat(r2.yzw) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 1948: mul
      r1.w = asuint(max(asfloat(r1.y), 0.0f)); // 1958: max
      r1.xyz = asuint(t_sky.Sample(s_sky_s, asfloat(r1.xwz)).xyz); // 1965: sample
      r0.w = asuint(saturate(dot(asfloat(r0.zz), float2(g_legacy_fog_colour_blend, g_legacy_fog_colour_blend)))); // 1976: dp2
      r1.xyz = asuint(mad(-(asfloat(r2.yzw)), float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f), asfloat(r1.xyz))); // 1984: mad
      r1.xyz = asuint(mad(asfloat(r0.www), asfloat(r1.xyz), asfloat(r3.xyz))); // 1997: mad
      r0.z = asuint(saturate(asfloat(r0.z) * asfloat(r2.x))); // 2006: mul
      r0.w = asuint(-(g_legacy_force_fog.x) + g_legacy_force_fog.y); // 2013: add
      r0.x = asuint(asfloat(r0.x) + -(g_legacy_force_fog.x)); // 2023: add
      r0.w = asuint(1.0f / asfloat(r0.w)); // 2032: div
      r0.x = asuint(saturate(asfloat(r0.w) * asfloat(r0.x))); // 2042: mul
      r0.w = asuint(mad(asfloat(r0.x), -2.0f, 3.0f)); // 2049: mad
      r0.x = asuint(asfloat(r0.x) * asfloat(r0.x)); // 2058: mul
      r0.x = asuint(asfloat(r0.x) * asfloat(r0.w)); // 2065: mul
      r0.w = asuint(-(asfloat(r0.z)) + 1.0f); // 2072: add
      r0.x = asuint(mad(asfloat(r0.x), asfloat(r0.w), asfloat(r0.z))); // 2080: mad
      r1.xyz = asuint(mad(asfloat(r1.xyz), asfloat(r5.www), -(asfloat(r5.xyz)))); // 2089: mad
      r5.xyz = asuint(mad(asfloat(r0.xxx), asfloat(r1.xyz), asfloat(r5.xyz))); // 2099: mad
    } else { // 2108: else
      r0.x = (g_vBakedShadowmapParams.x < 0.0f) ? 0xffffffffu : 0u; // 2109: lt
      if (r0.x != 0u) { // 2117: if
        r0.xzw = asuint(v6.xyz + -(float3(camera_position.x, camera_position.y, camera_position.z))); // 2120: add
        r0.x = asuint(dot(asfloat(r0.xzw), asfloat(r0.xzw))); // 2129: dp3
        r1.z = asuint(sqrt(asfloat(r0.x))); // 2136: sqrt
        r0.xz = asuint(asfloat(r0.wz) / asfloat(r1.zz)); // 2141: div
        r0.w = asuint(g_skybox_size * 0.5f); // 2148: mul
        r0.w = asuint(asfloat(r0.w) / abs(asfloat(r0.x))); // 2156: div
        r0.w = (asfloat(r0.w) < asfloat(r1.z)) ? 0xffffffffu : 0u; // 2164: lt
        r1.xw = (float2(9.99999997e-07f, 9.99999997e-07f) < abs(asfloat(r0.xz))) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 2171: lt
        r0.x = r0.w  & r1.x; // 2182: and
        r0.w = (camera_position.y < 200.0f) ? 0xffffffffu : 0u; // 2189: lt
        r0.x = r0.w  & r0.x; // 2197: and
        r2.y = asuint(mad(asfloat(r0.z), 100000.0f, camera_position.y)); // 2204: mad
        r2.z = 0x47c35000u; // 2214: mov
        r1.y = asuint(v6.y); // 2219: mov
        r2.yz = (r0.xx != uint2(0u, 0u)) ? r2.yz : r1.yz; // 2224: movc
        r0.x = asuint(min(g_fog_clear_distance, g_height_fog_clear_distance)); // 2233: min
        r0.x = asuint(min(asfloat(r0.x), asfloat(r2.z))); // 2242: min
        r0.w = asuint(mad(asfloat(r0.z), asfloat(r0.x), camera_position.y)); // 2249: mad
        r1.x = asuint(max(-(asfloat(r0.z)), 0.0f)); // 2259: max
        r1.y = asuint(asfloat(r1.x) * g_generating_light_probe); // 2267: mul
        r1.z = asuint(-(asfloat(r0.w)) + asfloat(r2.y)); // 2275: add
        r3.x = asuint(mad(asfloat(r1.y), asfloat(r1.z), asfloat(r0.w))); // 2283: mad
        r0.x = asuint(-(asfloat(r0.x)) + asfloat(r2.z)); // 2292: add
        r0.w = asuint(mad(-(asfloat(r1.x)), g_generating_light_probe, 1.0f)); // 2300: mad
        r1.z = asuint(asfloat(r0.w) * asfloat(r0.x)); // 2311: mul
        r2.z = asuint(-(g_fog_height_top) + g_fog_height_bottom); // 2318: add
        r2.z = asuint(mad(g_height_fog_falloff, asfloat(r2.z), g_fog_height_top)); // 2328: mad
        r3.w = asuint(-(asfloat(r3.x)) + asfloat(r2.z)); // 2339: add
        r3.w = asuint(asfloat(r3.w) / asfloat(r0.z)); // 2347: div
        r3.w = asuint(max(asfloat(r3.w), 0.0f)); // 2354: max
        r2.w = asuint(min(asfloat(r1.z), asfloat(r3.w))); // 2361: min
        r3.w = (9.99999997e-07f < asfloat(r0.z)) ? 0xffffffffu : 0u; // 2368: lt
        r2.x = asuint(max(asfloat(r2.z), asfloat(r3.x))); // 2375: max
        r4.x = (asfloat(r0.z) < -9.99999997e-07f) ? 0xffffffffu : 0u; // 2382: lt
        r3.y = asuint(max(asfloat(r2.z), asfloat(r2.y))); // 2389: max
        r3.z = asuint(mad(asfloat(r0.x), asfloat(r0.w), -(asfloat(r2.w)))); // 2396: mad
        r1.xy = uint2(0x00000000u, 0x00000000u); // 2406: mov
        r1.xyz = (r4.xxx != uint3(0u, 0u, 0u)) ? r3.xyz : r1.xyz; // 2414: movc
        r1.xyz = (r3.www != uint3(0u, 0u, 0u)) ? r2.xyw : r1.xyz; // 2423: movc
        r0.x = asuint(-(asfloat(r2.z)) + g_fog_height_top); // 2432: add
        r0.x = asuint(asfloat(r0.x) * 0.100000001f); // 2441: mul
        r0.w = (0.0f != asfloat(r0.x)) ? 0xffffffffu : 0u; // 2448: ne
        r0.w = r0.w  & r1.w; // 2458: and
        r1.xy = asuint(-(asfloat(r2.zz)) + asfloat(r1.xy)); // 2465: add
        r1.xy = asuint(max(asfloat(r1.xy), float2(0.0f, 0.0f))); // 2473: max
        r1.xy = asuint(-(asfloat(r1.xy)) / asfloat(r0.xx)); // 2483: div
        r1.xy = asuint(asfloat(r1.xy) * float2(1.44269502f, 1.44269502f)); // 2491: mul
        r1.xy = asuint(exp2(asfloat(r1.xy))); // 2501: exp
        r1.x = asuint(-(asfloat(r1.y)) + asfloat(r1.x)); // 2506: add
        r0.x = asuint(asfloat(r0.x) * asfloat(r1.x)); // 2514: mul
        r0.x = asuint(asfloat(r0.x) / asfloat(r0.z)); // 2521: div
        r0.x = asuint(asfloat(r0.x) + asfloat(r1.z)); // 2528: add
        r0.x = (r0.w != 0u) ? r0.x : r1.z; // 2535: movc
        r0.x = asuint(asfloat(r0.x) * g_fog_density_height); // 2544: mul
        r0.x = asuint(asfloat(r0.x) * -1.44269502f); // 2552: mul
        r0.x = asuint(exp2(asfloat(r0.x))); // 2559: exp
        r0.z = asuint(-(asfloat(r0.x)) + 1.0f); // 2564: add
        r1.xyz = asuint(asfloat(r0.zzz) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 2572: mul
        r1.xyz = asuint(asfloat(r5.www) * asfloat(r1.xyz)); // 2580: mul
        r5.xyz = asuint(mad(asfloat(r5.xyz), asfloat(r0.xxx), asfloat(r1.xyz))); // 2587: mad
      } else { // 2596: else
        r1.xyzw = asuint(v6.yxyz + -(float4(camera_position.y, camera_position.x, camera_position.y, camera_position.z))); // 2597: add
        r0.x = asuint(dot(asfloat(r1.yzw), asfloat(r1.yzw))); // 2606: dp3
        r0.x = asuint(sqrt(asfloat(r0.x))); // 2613: sqrt
        r1.xyzw = asuint(asfloat(r1.xyzw) / asfloat(r0.xxxx)); // 2618: div
        r0.z = asuint(min(g_fog_clear_distance, g_height_fog_clear_distance)); // 2625: min
        r0.w = asuint(mad(asfloat(r1.z), asfloat(r0.z), camera_position.y)); // 2634: mad
        r2.x = asuint(max(-(asfloat(r1.x)), 0.0f)); // 2644: max
        r2.x = asuint(asfloat(r2.x) * g_generating_light_probe); // 2652: mul
        r2.y = asuint(-(asfloat(r0.w)) + v6.y); // 2660: add
        r0.w = asuint(mad(asfloat(r2.x), asfloat(r2.y), asfloat(r0.w))); // 2668: mad
        r0.x = asuint(-(asfloat(r0.z)) + asfloat(r0.x)); // 2677: add
        r0.x = asuint(max(asfloat(r0.x), 0.0f)); // 2685: max
        r0.z = asuint(mad(g_fog_noise, -0.600000024f, 1.0f)); // 2692: mad
        r2.x = asuint(asfloat(r0.w) + -(g_atmosphere_bottom)); // 2702: add
        r2.y = asuint(max(asfloat(r2.x), 0.0f)); // 2711: max
        r2.z = asuint(asfloat(r2.y) + 6371000.0f); // 2718: add
        r2.w = asuint(mad(asfloat(r2.z), asfloat(r2.z), -4.07171624e+13f)); // 2725: mad
        r3.x = asuint(asfloat(r2.z) * -(sun_direction.y)); // 2734: mul
        r2.w = asuint(mad(asfloat(r3.x), asfloat(r3.x), -(asfloat(r2.w)))); // 2743: mad
        r2.w = asuint(sqrt(asfloat(r2.w))); // 2753: sqrt
        r2.z = asuint(mad(-(asfloat(r2.z)), -(sun_direction.y), asfloat(r2.w))); // 2758: mad
        r2.y = asuint(-(asfloat(r2.y)) + 10000.0f); // 2770: add
        r2.y = asuint(asfloat(r2.y) / asfloat(r2.z)); // 2778: div
        r2.y = asuint(min(asfloat(r2.y), 1.0f)); // 2785: min
        r2.z = asuint(-(g_fog_height_top) + g_fog_height_bottom); // 2792: add
        r2.z = asuint(mad(g_height_fog_falloff, asfloat(r2.z), g_fog_height_top)); // 2802: mad
        r2.w = (9.99999968e-21f < abs(asfloat(r2.y))) ? 0xffffffffu : 0u; // 2813: lt
        r3.x = asuint(max(asfloat(r0.w), g_fog_height_bottom)); // 2821: max
        r3.x = asuint(asfloat(r2.z) + -(asfloat(r3.x))); // 2829: add
        r3.y = asuint(asfloat(r3.x) / asfloat(r2.y)); // 2837: div
        r3.y = asuint(max(asfloat(r3.y), 0.0f)); // 2844: max
        r0.z = asuint(asfloat(r0.z) * g_fog_density_height); // 2851: mul
        r0.z = asuint(asfloat(r0.z) * 0.0125000002f); // 2859: mul
        r3.z = asuint(max(asfloat(r0.w), asfloat(r2.z))); // 2866: max
        r3.w = asuint(-(asfloat(r3.z)) + g_fog_height_top); // 2873: add
        r3.w = asuint(asfloat(r3.w) / asfloat(r2.y)); // 2882: div
        r3.w = asuint(max(asfloat(r3.w), 0.0f)); // 2889: max
        r3.yw = (r2.ww != uint2(0u, 0u)) ? r3.yw : uint2(0x49742400u, 0x49742400u); // 2896: movc
        r4.x = asuint(-(asfloat(r2.z)) + g_fog_height_top); // 2908: add
        r4.y = (0.0f != asfloat(r4.x)) ? 0xffffffffu : 0u; // 2917: ne
        r4.z = asuint(1.0f / asfloat(r4.x)); // 2927: div
        r4.y = r4.z  & r4.y; // 2937: and
        r3.z = asuint(min(asfloat(r3.z), g_fog_height_top)); // 2944: min
        r3.z = asuint(-(asfloat(r3.z)) + g_fog_height_top); // 2952: add
        r4.z = asuint(asfloat(r2.y) * 0.5f); // 2961: mul
        r3.z = asuint(mad(-(asfloat(r4.z)), asfloat(r3.w), asfloat(r3.z))); // 2968: mad
        r3.z = asuint(asfloat(r3.w) * asfloat(r3.z)); // 2978: mul
        r3.z = asuint(asfloat(r4.y) * asfloat(r3.z)); // 2985: mul
        r3.z = asuint(asfloat(r0.z) * asfloat(r3.z)); // 2992: mul
        r3.y = asuint(mad(asfloat(r0.z), asfloat(r3.y), asfloat(r3.z))); // 2999: mad
        r3.y = asuint(asfloat(r3.y) * 1.44269502f); // 3008: mul
        r3.y = asuint(exp2(asfloat(r3.y))); // 3015: exp
        r3.z = asuint(min(g_rayleigh_density.z, g_rayleigh_density.y)); // 3020: min
        r3.z = asuint(min(asfloat(r3.z), g_rayleigh_density.x)); // 3029: min
        r3.w = asuint(camera_position.y + -(g_atmosphere_bottom)); // 3037: add
        r3.w = asuint(max(asfloat(r3.w), 0.0f)); // 3047: max
        r6.xy = asuint(float2(g_atmosphere_height_multiplier, g_atmosphere_height_multiplier) * float2(6994.0f, 1200.0f)); // 3054: mul
        r6.zw = asuint(-(asfloat(r3.ww)) / asfloat(r6.xy)); // 3065: div
        r6.zw = asuint(asfloat(r6.zw) * float2(1.44269502f, 1.44269502f)); // 3073: mul
        r6.zw = asuint(exp2(asfloat(r6.zw))); // 3083: exp
        r6.zw = asuint(asfloat(r6.xy) * asfloat(r6.zw)); // 3088: mul
        r3.w = asuint(max(asfloat(r2.y), 0.0f)); // 3095: max
        r6.zw = asuint(asfloat(r6.zw) / asfloat(r3.ww)); // 3102: div
        r3.w = asuint(asfloat(r6.w) * 1.99999995e-05f); // 3109: mul
        r3.w = asuint(mad(asfloat(r3.z), asfloat(r6.z), asfloat(r3.w))); // 3116: mad
        r3.w = asuint(asfloat(r3.w) * 1.44269502f); // 3125: mul
        r3.w = asuint(exp2(asfloat(r3.w))); // 3132: exp
        r7.xyz = asuint(float3(sun_colour.x, sun_colour.y, sun_colour.z) * float3(7.61524207e-05f, 7.61524207e-05f, 7.61524207e-05f)); // 3137: mul
        r4.w = (asfloat(r0.w) < asfloat(r2.z)) ? 0xffffffffu : 0u; // 3148: lt
        r8.xyzw = (float4(9.99999968e-21f, 9.99999972e-10f, 9.99999997e-07f, 0.00100000005f) < abs(asfloat(r1.zzzz))) ? uint4(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu) : uint4(0u, 0u, 0u, 0u); // 3155: lt
        r3.x = asuint(asfloat(r3.x) / asfloat(r1.z)); // 3166: div
        r3.x = asuint(max(asfloat(r3.x), 0.0f)); // 3173: max
        r3.x = (r8.x != 0u) ? r3.x : 0x49742400u; // 3180: movc
        r3.x = asuint(asfloat(r0.z) * asfloat(r3.x)); // 3189: mul
        r3.x = asuint(asfloat(r3.x) * -1.44269502f); // 3196: mul
        r3.x = asuint(exp2(asfloat(r3.x))); // 3203: exp
        r3.x = (r4.w != 0u) ? r3.x : 0x3f800000u; // 3208: movc
        r4.w = (asfloat(r2.z) < asfloat(r0.w)) ? 0xffffffffu : 0u; // 3217: lt
        r6.w = (asfloat(r0.w) >= asfloat(r2.z)) ? 0xffffffffu : 0u; // 3224: ge
        r7.w = (g_fog_height_top >= asfloat(r0.w)) ? 0xffffffffu : 0u; // 3231: ge
        r6.w = r6.w  & r7.w; // 3239: and
        r9.xy = asuint(-(asfloat(r0.ww)) + float2(g_fog_height_bottom, g_fog_height_top)); // 3246: add
        r9.xy = asuint(asfloat(r9.xy) / asfloat(r1.zz)); // 3255: div
        r9.xy = asuint(max(asfloat(r9.xy), float2(0.0f, 0.0f))); // 3262: max
        r7.w = asuint(min(asfloat(r9.y), 1000000.0f)); // 3272: min
        r8.x = r6.w  & 0x49742400u; // 3279: and
        r7.w = (r8.z != 0u) ? r7.w : r8.x; // 3286: movc
        r8.x = asuint(-(asfloat(r0.w)) + asfloat(r2.z)); // 3295: add
        r8.x = asuint(asfloat(r8.x) / asfloat(r1.z)); // 3303: div
        r8.x = asuint(max(asfloat(r8.x), 0.0f)); // 3310: max
        r9.z = asuint(min(asfloat(r8.x), 1000000.0f)); // 3317: min
        r9.z = r8.z  & r9.z; // 3324: and
        r9.w = asuint(max(asfloat(r7.w), asfloat(r9.z))); // 3331: max
        r7.w = asuint(min(asfloat(r7.w), asfloat(r9.z))); // 3338: min
        r9.z = asuint(mad(asfloat(r7.w), asfloat(r1.z), asfloat(r0.w))); // 3345: mad
        r7.w = asuint(-(asfloat(r7.w)) + asfloat(r9.w)); // 3354: add
        r9.z = asuint(-(asfloat(r9.z)) + g_fog_height_top); // 3362: add
        r10.xy = asuint(asfloat(r1.zz) * float2(0.5f, -0.5f)); // 3371: mul
        r9.z = asuint(mad(-(asfloat(r10.x)), asfloat(r7.w), asfloat(r9.z))); // 3381: mad
        r7.w = asuint(asfloat(r7.w) * asfloat(r9.z)); // 3391: mul
        r7.w = asuint(asfloat(r4.y) * asfloat(r7.w)); // 3398: mul
        r9.zw = r6.ww  | r8.yw; // 3405: or
        r7.w = asuint(asfloat(r0.z) * asfloat(r7.w)); // 3412: mul
        r7.w = r7.w  & r9.z; // 3419: and
        r4.w = r4.w  & r7.w; // 3426: and
        r7.w = (asfloat(r0.w) >= g_fog_height_bottom) ? 0xffffffffu : 0u; // 3433: ge
        r8.y = (asfloat(r2.z) >= asfloat(r0.w)) ? 0xffffffffu : 0u; // 3441: ge
        r7.w = r7.w  & r8.y; // 3448: and
        r8.x = asuint(min(asfloat(r0.x), asfloat(r8.x))); // 3455: min
        r8.y = r0.x  & r7.w; // 3462: and
        r8.y = (r8.z != 0u) ? r8.x : r8.y; // 3469: movc
        r9.xy = asuint(min(asfloat(r0.xx), asfloat(r9.xy))); // 3478: min
        r9.x = r8.z  & r9.x; // 3485: and
        r9.z = asuint(max(asfloat(r8.y), asfloat(r9.x))); // 3492: max
        r10.z = asuint(min(asfloat(r8.y), asfloat(r9.x))); // 3499: min
        r10.w = asuint(mad(asfloat(r10.z), asfloat(r1.z), asfloat(r0.w))); // 3506: mad
        r11.x = asuint(mad(asfloat(r9.z), asfloat(r1.z), asfloat(r0.w))); // 3515: mad
        r9.z = asuint(asfloat(r9.z) + -(asfloat(r10.z))); // 3524: add
        r10.z = asuint(asfloat(r2.z) + -(asfloat(r10.w))); // 3532: add
        r11.y = asuint(asfloat(r10.z) / asfloat(r2.y)); // 3540: div
        r10.w = asuint(-(asfloat(r10.w)) + g_fog_height_bottom); // 3547: add
        r11.z = asuint(asfloat(r10.w) / asfloat(r2.y)); // 3556: div
        r11.yz = asuint(max(asfloat(r11.yz), float2(0.0f, 0.0f))); // 3563: max
        r11.y = asuint(-(asfloat(r11.z)) + asfloat(r11.y)); // 3573: add
        r11.z = asuint(asfloat(r2.z) + -(asfloat(r11.x))); // 3581: add
        r11.w = asuint(asfloat(r11.z) / asfloat(r2.y)); // 3589: div
        r11.w = asuint(max(asfloat(r11.w), 0.0f)); // 3596: max
        r11.x = asuint(-(asfloat(r11.x)) + g_fog_height_bottom); // 3603: add
        r12.x = asuint(asfloat(r11.x) / asfloat(r2.y)); // 3612: div
        r12.x = asuint(max(asfloat(r12.x), 0.0f)); // 3619: max
        r11.w = asuint(asfloat(r11.w) + -(asfloat(r12.x))); // 3626: add
        r11.w = asuint(asfloat(r9.z) + asfloat(r11.w)); // 3634: add
        r11.w = asuint(-(asfloat(r11.y)) + asfloat(r11.w)); // 3641: add
        r12.xyz = asuint(asfloat(r9.zzz) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 3649: mul
        r12.w = (abs(asfloat(r11.w)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 3657: lt
        r13.x = (abs(asfloat(r9.z)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 3665: lt
        r12.w = r12.w  | r13.x; // 3673: or
        r11.y = asuint(-(asfloat(r0.z)) * asfloat(r11.y)); // 3680: mul
        r11.y = asuint(asfloat(r11.y) * 1.44269502f); // 3688: mul
        r11.y = asuint(exp2(asfloat(r11.y))); // 3695: exp
        r11.y = asuint(asfloat(r11.y) / asfloat(r11.w)); // 3700: div
        r11.w = asuint(-(asfloat(r0.z)) * asfloat(r11.w)); // 3707: mul
        r11.w = asuint(asfloat(r11.w) * 1.44269502f); // 3715: mul
        r11.w = asuint(exp2(asfloat(r11.w))); // 3722: exp
        r11.w = asuint(-(asfloat(r11.w)) + 1.0f); // 3727: add
        r11.y = asuint(asfloat(r11.w) * asfloat(r11.y)); // 3735: mul
        r11.y = (r12.w != 0u) ? r0.z : r11.y; // 3742: movc
        r13.yzw = asuint(asfloat(r11.yyy) * asfloat(r12.xyz)); // 3751: mul
        r7.w = r8.w  | r7.w; // 3758: or
        r13.yzw = r13.yzw  & r7.www; // 3765: and
        r6.w = r0.x  & r6.w; // 3772: and
        r6.w = (r8.z != 0u) ? r9.y : r6.w; // 3779: movc
        r8.x = r8.x  & r8.z; // 3788: and
        r8.z = asuint(max(asfloat(r6.w), asfloat(r8.x))); // 3795: max
        r6.w = asuint(min(asfloat(r6.w), asfloat(r8.x))); // 3802: min
        r8.x = asuint(mad(asfloat(r6.w), asfloat(r1.z), asfloat(r0.w))); // 3809: mad
        r8.w = asuint(mad(asfloat(r8.z), asfloat(r1.z), asfloat(r0.w))); // 3818: mad
        r6.w = asuint(-(asfloat(r6.w)) + asfloat(r8.z)); // 3827: add
        r8.z = asuint(-(asfloat(r8.x)) + g_fog_height_top); // 3835: add
        r9.y = asuint(asfloat(r8.z) / asfloat(r2.y)); // 3844: div
        r9.y = asuint(max(asfloat(r9.y), 0.0f)); // 3851: max
        r8.x = asuint(asfloat(r2.z) + -(asfloat(r8.x))); // 3858: add
        r11.y = asuint(asfloat(r8.x) / asfloat(r2.y)); // 3866: div
        r11.y = asuint(max(asfloat(r11.y), 0.0f)); // 3873: max
        r9.y = asuint(asfloat(r9.y) + -(asfloat(r11.y))); // 3880: add
        r11.y = asuint(-(asfloat(r8.w)) + g_fog_height_top); // 3888: add
        r11.w = asuint(asfloat(r11.y) / asfloat(r2.y)); // 3897: div
        r11.w = asuint(max(asfloat(r11.w), 0.0f)); // 3904: max
        r8.w = asuint(asfloat(r2.z) + -(asfloat(r8.w))); // 3911: add
        r12.w = asuint(asfloat(r8.w) / asfloat(r2.y)); // 3919: div
        r12.w = asuint(max(asfloat(r12.w), 0.0f)); // 3926: max
        r11.w = asuint(asfloat(r11.w) + -(asfloat(r12.w))); // 3933: add
        r11.w = asuint(-(asfloat(r9.y)) + asfloat(r11.w)); // 3941: add
        r11.w = asuint(asfloat(r11.w) / asfloat(r6.w)); // 3949: div
        r12.w = asuint(asfloat(r0.z) / asfloat(r4.x)); // 3956: div
        r14.x = asuint(mad(-(asfloat(r4.z)), asfloat(r9.y), asfloat(r8.z))); // 3963: mad
        r14.y = asuint(mad(-(asfloat(r4.z)), asfloat(r11.w), -(asfloat(r1.x)))); // 3973: mad
        r14.z = asuint(mad(asfloat(r11.w), asfloat(r14.y), asfloat(r10.y))); // 3984: mad
        r14.z = asuint(asfloat(r12.w) * asfloat(r14.z)); // 3993: mul
        r14.y = asuint(mad(asfloat(r9.y), asfloat(r14.y), asfloat(r8.z))); // 4000: mad
        r11.w = asuint(mad(asfloat(r11.w), asfloat(r14.x), asfloat(r14.y))); // 4009: mad
        r11.w = asuint(asfloat(r12.w) * asfloat(r11.w)); // 4018: mul
        r9.y = asuint(asfloat(r9.y) * asfloat(r14.x)); // 4025: mul
        r9.y = asuint(asfloat(r12.w) * asfloat(r9.y)); // 4032: mul
        r14.x = asuint(asfloat(r8.z) * asfloat(r12.w)); // 4039: mul
        r14.y = asuint(-(asfloat(r1.x)) * asfloat(r12.w)); // 4046: mul
        r14.w = asuint(sqrt(abs(asfloat(r14.z)))); // 4054: sqrt
        r15.x = asuint(mad(asfloat(r14.z), asfloat(r6.w), asfloat(r11.w))); // 4060: mad
        r15.x = asuint(-(asfloat(r6.w)) * asfloat(r15.x)); // 4069: mul
        r15.x = asuint(asfloat(r15.x) * 1.44269502f); // 4077: mul
        r15.x = asuint(exp2(asfloat(r15.x))); // 4084: exp
        r9.y = asuint(asfloat(r9.y) * -1.44269502f); // 4089: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 4096: exp
        r15.y = asuint(asfloat(r14.z) * 4.0f); // 4101: mul
        r15.z = asuint(1.0f / asfloat(r15.y)); // 4108: div
        r15.w = asuint(asfloat(r14.w) + asfloat(r14.w)); // 4118: add
        r15.w = asuint(1.0f / asfloat(r15.w)); // 4125: div
        r16.x = asuint(asfloat(r11.w) * asfloat(r15.w)); // 4135: mul
        r16.y = asuint(asfloat(r14.z) + asfloat(r14.z)); // 4142: add
        r16.z = asuint(mad(asfloat(r16.y), asfloat(r6.w), asfloat(r11.w))); // 4149: mad
        r15.w = asuint(asfloat(r15.w) * asfloat(r16.z)); // 4158: mul
        r16.w = (0.0f < asfloat(r14.z)) ? 0xffffffffu : 0u; // 4165: lt
        if (r16.w != 0u) { // 4172: if
          r16.w = asuint(abs(asfloat(r16.x)) * abs(asfloat(r16.x))); // 4175: mul
          r17.x = asuint(abs(asfloat(r16.x)) * asfloat(r16.w)); // 4184: mul
          r17.y = asuint(abs(asfloat(r16.x)) * asfloat(r17.x)); // 4192: mul
          r17.z = asuint(mad(abs(asfloat(r16.x)), 0.278393f, 1.0f)); // 4200: mad
          r16.w = asuint(mad(asfloat(r16.w), 0.230388999f, asfloat(r17.z))); // 4210: mad
          r16.w = asuint(mad(asfloat(r17.x), 0.000972000009f, asfloat(r16.w))); // 4219: mad
          r16.w = asuint(mad(asfloat(r17.y), 0.0781079978f, asfloat(r16.w))); // 4228: mad
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 4237: mul
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 4244: mul
          r16.w = asuint(1.0f / asfloat(r16.w)); // 4251: div
          r16.w = asuint(-(asfloat(r16.w)) + 1.0f); // 4261: add
          r17.x = (0.0f < asfloat(r16.x)) ? 0xffffffffu : 0u; // 4269: lt
          r17.y = (asfloat(r16.x) < 0.0f) ? 0xffffffffu : 0u; // 4276: lt
          r17.x = asuint(-(asint(r17.x)) + asint(r17.y)); // 4283: iadd
          r17.x = asuint((float)(asint(r17.x))); // 4291: itof
          r17.x = asuint(asfloat(r16.w) * asfloat(r17.x)); // 4296: mul
          r16.w = asuint(abs(asfloat(r15.w)) * abs(asfloat(r15.w))); // 4303: mul
          r17.z = asuint(abs(asfloat(r15.w)) * asfloat(r16.w)); // 4312: mul
          r17.w = asuint(abs(asfloat(r15.w)) * asfloat(r17.z)); // 4320: mul
          r18.x = asuint(mad(abs(asfloat(r15.w)), 0.278393f, 1.0f)); // 4328: mad
          r16.w = asuint(mad(asfloat(r16.w), 0.230388999f, asfloat(r18.x))); // 4338: mad
          r16.w = asuint(mad(asfloat(r17.z), 0.000972000009f, asfloat(r16.w))); // 4347: mad
          r16.w = asuint(mad(asfloat(r17.w), 0.0781079978f, asfloat(r16.w))); // 4356: mad
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 4365: mul
          r16.w = asuint(asfloat(r16.w) * asfloat(r16.w)); // 4372: mul
          r16.w = asuint(1.0f / asfloat(r16.w)); // 4379: div
          r16.w = asuint(-(asfloat(r16.w)) + 1.0f); // 4389: add
          r17.z = (0.0f < asfloat(r15.w)) ? 0xffffffffu : 0u; // 4397: lt
          r17.w = (asfloat(r15.w) < 0.0f) ? 0xffffffffu : 0u; // 4404: lt
          r17.z = asuint(-(asint(r17.z)) + asint(r17.w)); // 4411: iadd
          r17.z = asuint((float)(asint(r17.z))); // 4419: itof
          r17.y = asuint(asfloat(r16.w) * asfloat(r17.z)); // 4424: mul
        } else { // 4431: else
          r18.x = asuint(asfloat(r16.x) * asfloat(r16.x)); // 4432: mul
          r16.w = asuint(asfloat(r18.x) * 1.44269502f); // 4439: mul
          r16.w = asuint(exp2(asfloat(r16.w))); // 4446: exp
          r16.w = asuint(asfloat(r16.w) * 1.12837923f); // 4451: mul
          r18.y = asuint(asfloat(r18.x) * asfloat(r18.x)); // 4458: mul
          r18.zw = asuint(asfloat(r18.xy) * asfloat(r18.yy)); // 4465: mul
          r17.z = asuint(asfloat(r18.y) * asfloat(r18.z)); // 4472: mul
          r17.w = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r18.xyzw))); // 4479: dp4
          r17.w = asuint(asfloat(r17.w) + 1.0f); // 4489: add
          r18.x = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r18.xyzw))); // 4496: dp4
          r18.x = asuint(asfloat(r18.x) + 1.0f); // 4506: add
          r17.z = asuint(mad(asfloat(r17.z), 0.00257857703f, asfloat(r18.x))); // 4513: mad
          r16.x = asuint(asfloat(r16.x) * asfloat(r17.w)); // 4522: mul
          r16.x = asuint(asfloat(r16.x) / asfloat(r17.z)); // 4529: div
          r17.x = asuint(asfloat(r16.x) * asfloat(r16.w)); // 4536: mul
          r18.x = asuint(asfloat(r15.w) * asfloat(r15.w)); // 4543: mul
          r16.x = asuint(asfloat(r18.x) * 1.44269502f); // 4550: mul
          r16.x = asuint(exp2(asfloat(r16.x))); // 4557: exp
          r16.x = asuint(asfloat(r16.x) * 1.12837923f); // 4562: mul
          r18.y = asuint(asfloat(r18.x) * asfloat(r18.x)); // 4569: mul
          r18.zw = asuint(asfloat(r18.xy) * asfloat(r18.yy)); // 4576: mul
          r16.w = asuint(asfloat(r18.y) * asfloat(r18.z)); // 4583: mul
          r17.z = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r18.xyzw))); // 4590: dp4
          r17.w = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r18.xyzw))); // 4600: dp4
          r17.zw = asuint(asfloat(r17.zw) + float2(1.0f, 1.0f)); // 4610: add
          r16.w = asuint(mad(asfloat(r16.w), 0.00257857703f, asfloat(r17.w))); // 4620: mad
          r15.w = asuint(asfloat(r15.w) * asfloat(r17.z)); // 4629: mul
          r15.w = asuint(asfloat(r15.w) / asfloat(r16.w)); // 4636: div
          r17.y = asuint(asfloat(r15.w) * asfloat(r16.x)); // 4643: mul
        } // 4650: endif
        r15.w = asuint(asfloat(r11.w) * asfloat(r14.y)); // 4651: mul
        r16.x = asuint(mad(asfloat(r16.y), asfloat(r14.x), -(asfloat(r15.w)))); // 4658: mad
        r16.x = asuint(asfloat(r16.x) * 1.7724539f); // 4668: mul
        r16.w = asuint(asfloat(r11.w) * asfloat(r11.w)); // 4675: mul
        r17.z = asuint(asfloat(r15.z) * asfloat(r16.w)); // 4682: mul
        r17.z = asuint(asfloat(r17.z) * 1.44269502f); // 4689: mul
        r17.z = asuint(exp2(asfloat(r17.z))); // 4696: exp
        r17.z = asuint(asfloat(r16.x) * asfloat(r17.z)); // 4701: mul
        r17.x = asuint(asfloat(r17.x) * asfloat(r17.z)); // 4708: mul
        r16.z = asuint(asfloat(r16.z) * asfloat(r16.z)); // 4715: mul
        r15.z = asuint(asfloat(r15.z) * asfloat(r16.z)); // 4722: mul
        r15.z = asuint(asfloat(r15.z) * 1.44269502f); // 4729: mul
        r15.z = asuint(exp2(asfloat(r15.z))); // 4736: exp
        r15.z = asuint(asfloat(r15.z) * asfloat(r16.x)); // 4741: mul
        r15.z = asuint(asfloat(r17.y) * asfloat(r15.z)); // 4748: mul
        r14.w = asuint(asfloat(r14.w) * asfloat(r15.y)); // 4755: mul
        r15.y = asuint(asfloat(r14.y) / asfloat(r16.y)); // 4762: div
        r14.z = (9.99999975e-06f < abs(asfloat(r14.z))) ? 0xffffffffu : 0u; // 4769: lt
        r16.x = asuint(-(asfloat(r15.x)) + 1.0f); // 4777: add
        r16.y = asuint(asfloat(r15.y) * asfloat(r16.x)); // 4785: mul
        r15.x = asuint(mad(asfloat(r15.x), asfloat(r15.z), -(asfloat(r17.x)))); // 4792: mad
        r14.w = asuint(asfloat(r15.x) / asfloat(r14.w)); // 4802: div
        r14.w = asuint(mad(asfloat(r15.y), asfloat(r16.x), asfloat(r14.w))); // 4809: mad
        r15.x = (asfloat(r15.x) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 4818: lt
        r15.y = (9.99999997e-07f < abs(asfloat(r11.w))) ? 0xffffffffu : 0u; // 4825: lt
        r15.z = asuint(mad(asfloat(r11.w), asfloat(r14.x), asfloat(r14.y))); // 4833: mad
        r15.w = asuint(mad(asfloat(r15.w), asfloat(r6.w), asfloat(r15.z))); // 4842: mad
        r11.w = asuint(asfloat(r6.w) * -(asfloat(r11.w))); // 4851: mul
        r11.w = asuint(asfloat(r11.w) * 1.44269502f); // 4859: mul
        r11.w = asuint(exp2(asfloat(r11.w))); // 4866: exp
        r11.w = asuint(mad(-(asfloat(r15.w)), asfloat(r11.w), asfloat(r15.z))); // 4871: mad
        r11.w = asuint(asfloat(r11.w) / asfloat(r16.w)); // 4881: div
        r15.z = asuint(asfloat(r6.w) * asfloat(r14.y)); // 4888: mul
        r15.z = asuint(mad(asfloat(r15.z), 0.5f, asfloat(r14.x))); // 4895: mad
        r15.z = asuint(asfloat(r6.w) * asfloat(r15.z)); // 4904: mul
        r11.w = (r15.y != 0u) ? r11.w : r15.z; // 4911: movc
        r11.w = (r15.x != 0u) ? r16.y : r11.w; // 4920: movc
        r11.w = (r14.z != 0u) ? r14.w : r11.w; // 4929: movc
        r9.y = asuint(asfloat(r9.y) * asfloat(r11.w)); // 4938: mul
        r11.w = (1.0f < asfloat(r9.y)) ? 0xffffffffu : 0u; // 4945: lt
        r9.y = (r11.w != 0u) ? 0x3f800000u : r9.y; // 4952: movc
        r11.w = asuint(asfloat(r0.z) * asfloat(r4.x)); // 4961: mul
        r11.w = (0.0f < asfloat(r11.w)) ? 0xffffffffu : 0u; // 4968: lt
        r14.z = (0.0f < asfloat(r6.w)) ? 0xffffffffu : 0u; // 4975: lt
        r11.w = r11.w  & r14.z; // 4982: and
        r9.w = r9.w  & r11.w; // 4989: and
        r9.y = r9.y  & r9.w; // 4996: and
        r15.xyw = asuint(asfloat(r9.yyy) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 5003: mul
        r9.y = asuint(asfloat(r4.x) / asfloat(r2.y)); // 5011: div
        r9.y = asuint(max(asfloat(r9.y), 0.0f)); // 5018: max
        r9.y = (r2.w != 0u) ? r9.y : 0x49742400u; // 5025: movc
        r11.w = asuint(min(asfloat(r2.z), g_fog_height_top)); // 5034: min
        r11.w = asuint(-(asfloat(r11.w)) + g_fog_height_top); // 5042: add
        r14.z = asuint(mad(-(asfloat(r4.z)), asfloat(r9.y), asfloat(r11.w))); // 5051: mad
        r9.y = asuint(asfloat(r9.y) * asfloat(r14.z)); // 5061: mul
        r9.y = asuint(asfloat(r4.y) * asfloat(r9.y)); // 5068: mul
        r9.y = asuint(mad(asfloat(r0.z), asfloat(r9.y), asfloat(r4.w))); // 5075: mad
        r9.y = asuint(asfloat(r9.y) * -1.44269502f); // 5084: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 5091: exp
        r15.xyw = asuint(asfloat(r3.xxx) * asfloat(r15.xyw)); // 5096: mul
        r13.yzw = asuint(mad(asfloat(r13.yzw), asfloat(r9.yyy), asfloat(r15.xyw))); // 5103: mad
        r9.y = asuint(max(asfloat(r10.z), 0.0f)); // 5112: max
        r10.z = asuint(max(asfloat(r10.w), 0.0f)); // 5119: max
        r9.y = asuint(asfloat(r9.y) + -(asfloat(r10.z))); // 5126: add
        r10.zw = asuint(max(asfloat(r11.zx), float2(0.0f, 0.0f))); // 5134: max
        r10.z = asuint(-(asfloat(r10.w)) + asfloat(r10.z)); // 5144: add
        r9.z = asuint(asfloat(r9.z) + asfloat(r10.z)); // 5152: add
        r9.z = asuint(-(asfloat(r9.y)) + asfloat(r9.z)); // 5159: add
        r10.z = (abs(asfloat(r9.z)) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 5167: lt
        r10.z = r13.x  | r10.z; // 5175: or
        r9.y = asuint(-(asfloat(r0.z)) * asfloat(r9.y)); // 5182: mul
        r9.y = asuint(asfloat(r9.y) * 1.44269502f); // 5190: mul
        r9.y = asuint(exp2(asfloat(r9.y))); // 5197: exp
        r9.y = asuint(asfloat(r9.y) / asfloat(r9.z)); // 5202: div
        r9.z = asuint(-(asfloat(r0.z)) * asfloat(r9.z)); // 5209: mul
        r9.z = asuint(asfloat(r9.z) * 1.44269502f); // 5217: mul
        r9.z = asuint(exp2(asfloat(r9.z))); // 5224: exp
        r9.z = asuint(-(asfloat(r9.z)) + 1.0f); // 5229: add
        r9.y = asuint(asfloat(r9.z) * asfloat(r9.y)); // 5237: mul
        r9.y = (r10.z != 0u) ? r0.z : r9.y; // 5244: movc
        r12.xyz = asuint(asfloat(r9.yyy) * asfloat(r12.xyz)); // 5253: mul
        r12.xyz = r7.www  & r12.xyz; // 5260: and
        r7.w = asuint(max(asfloat(r8.z), 0.0f)); // 5267: max
        r8.xw = asuint(max(asfloat(r8.xw), float2(0.0f, 0.0f))); // 5274: max
        r7.w = asuint(asfloat(r7.w) + -(asfloat(r8.x))); // 5284: add
        r8.x = asuint(max(asfloat(r11.y), 0.0f)); // 5292: max
        r8.x = asuint(-(asfloat(r8.w)) + asfloat(r8.x)); // 5299: add
        r8.x = asuint(-(asfloat(r7.w)) + asfloat(r8.x)); // 5307: add
        r8.x = asuint(asfloat(r8.x) / asfloat(r6.w)); // 5315: div
        r8.w = asuint(mad(-(asfloat(r7.w)), 0.5f, asfloat(r8.z))); // 5322: mad
        r1.x = asuint(mad(-(asfloat(r8.x)), 0.5f, -(asfloat(r1.x)))); // 5332: mad
        r9.y = asuint(mad(asfloat(r8.x), asfloat(r1.x), asfloat(r10.y))); // 5343: mad
        r9.y = asuint(asfloat(r12.w) * asfloat(r9.y)); // 5352: mul
        r1.x = asuint(mad(asfloat(r7.w), asfloat(r1.x), asfloat(r8.z))); // 5359: mad
        r1.x = asuint(mad(asfloat(r8.x), asfloat(r8.w), asfloat(r1.x))); // 5368: mad
        r1.x = asuint(asfloat(r12.w) * asfloat(r1.x)); // 5377: mul
        r7.w = asuint(asfloat(r7.w) * asfloat(r8.w)); // 5384: mul
        r7.w = asuint(asfloat(r12.w) * asfloat(r7.w)); // 5391: mul
        r8.x = asuint(sqrt(abs(asfloat(r9.y)))); // 5398: sqrt
        r8.w = asuint(mad(asfloat(r9.y), asfloat(r6.w), asfloat(r1.x))); // 5404: mad
        r8.w = asuint(-(asfloat(r6.w)) * asfloat(r8.w)); // 5413: mul
        r8.w = asuint(asfloat(r8.w) * 1.44269502f); // 5421: mul
        r8.w = asuint(exp2(asfloat(r8.w))); // 5428: exp
        r7.w = asuint(asfloat(r7.w) * -1.44269502f); // 5433: mul
        r7.w = asuint(exp2(asfloat(r7.w))); // 5440: exp
        r9.z = asuint(asfloat(r9.y) * 4.0f); // 5445: mul
        r10.y = asuint(1.0f / asfloat(r9.z)); // 5452: div
        r10.z = asuint(asfloat(r8.x) + asfloat(r8.x)); // 5462: add
        r10.z = asuint(1.0f / asfloat(r10.z)); // 5469: div
        r10.w = asuint(asfloat(r1.x) * asfloat(r10.z)); // 5479: mul
        r11.x = asuint(asfloat(r9.y) + asfloat(r9.y)); // 5486: add
        r11.y = asuint(mad(asfloat(r11.x), asfloat(r6.w), asfloat(r1.x))); // 5493: mad
        r10.z = asuint(asfloat(r10.z) * asfloat(r11.y)); // 5502: mul
        r11.z = (0.0f < asfloat(r9.y)) ? 0xffffffffu : 0u; // 5509: lt
        if (r11.z != 0u) { // 5516: if
          r11.z = asuint(abs(asfloat(r10.w)) * abs(asfloat(r10.w))); // 5519: mul
          r12.w = asuint(abs(asfloat(r10.w)) * asfloat(r11.z)); // 5528: mul
          r13.x = asuint(abs(asfloat(r10.w)) * asfloat(r12.w)); // 5536: mul
          r14.z = asuint(mad(abs(asfloat(r10.w)), 0.278393f, 1.0f)); // 5544: mad
          r11.z = asuint(mad(asfloat(r11.z), 0.230388999f, asfloat(r14.z))); // 5554: mad
          r11.z = asuint(mad(asfloat(r12.w), 0.000972000009f, asfloat(r11.z))); // 5563: mad
          r11.z = asuint(mad(asfloat(r13.x), 0.0781079978f, asfloat(r11.z))); // 5572: mad
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 5581: mul
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 5588: mul
          r11.z = asuint(1.0f / asfloat(r11.z)); // 5595: div
          r11.z = asuint(-(asfloat(r11.z)) + 1.0f); // 5605: add
          r12.w = (0.0f < asfloat(r10.w)) ? 0xffffffffu : 0u; // 5613: lt
          r13.x = (asfloat(r10.w) < 0.0f) ? 0xffffffffu : 0u; // 5620: lt
          r12.w = asuint(-(asint(r12.w)) + asint(r13.x)); // 5627: iadd
          r12.w = asuint((float)(asint(r12.w))); // 5635: itof
          r15.x = asuint(asfloat(r11.z) * asfloat(r12.w)); // 5640: mul
          r11.z = asuint(abs(asfloat(r10.z)) * abs(asfloat(r10.z))); // 5647: mul
          r12.w = asuint(abs(asfloat(r10.z)) * asfloat(r11.z)); // 5656: mul
          r13.x = asuint(abs(asfloat(r10.z)) * asfloat(r12.w)); // 5664: mul
          r14.z = asuint(mad(abs(asfloat(r10.z)), 0.278393f, 1.0f)); // 5672: mad
          r11.z = asuint(mad(asfloat(r11.z), 0.230388999f, asfloat(r14.z))); // 5682: mad
          r11.z = asuint(mad(asfloat(r12.w), 0.000972000009f, asfloat(r11.z))); // 5691: mad
          r11.z = asuint(mad(asfloat(r13.x), 0.0781079978f, asfloat(r11.z))); // 5700: mad
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 5709: mul
          r11.z = asuint(asfloat(r11.z) * asfloat(r11.z)); // 5716: mul
          r11.z = asuint(1.0f / asfloat(r11.z)); // 5723: div
          r11.z = asuint(-(asfloat(r11.z)) + 1.0f); // 5733: add
          r12.w = (0.0f < asfloat(r10.z)) ? 0xffffffffu : 0u; // 5741: lt
          r13.x = (asfloat(r10.z) < 0.0f) ? 0xffffffffu : 0u; // 5748: lt
          r12.w = asuint(-(asint(r12.w)) + asint(r13.x)); // 5755: iadd
          r12.w = asuint((float)(asint(r12.w))); // 5763: itof
          r15.y = asuint(asfloat(r11.z) * asfloat(r12.w)); // 5768: mul
        } else { // 5775: else
          r16.x = asuint(asfloat(r10.w) * asfloat(r10.w)); // 5776: mul
          r11.z = asuint(asfloat(r16.x) * 1.44269502f); // 5783: mul
          r11.z = asuint(exp2(asfloat(r11.z))); // 5790: exp
          r11.z = asuint(asfloat(r11.z) * 1.12837923f); // 5795: mul
          r16.y = asuint(asfloat(r16.x) * asfloat(r16.x)); // 5802: mul
          r16.zw = asuint(asfloat(r16.xy) * asfloat(r16.yy)); // 5809: mul
          r12.w = asuint(asfloat(r16.y) * asfloat(r16.z)); // 5816: mul
          r13.x = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r16.xyzw))); // 5823: dp4
          r13.x = asuint(asfloat(r13.x) + 1.0f); // 5833: add
          r14.z = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r16.xyzw))); // 5840: dp4
          r14.z = asuint(asfloat(r14.z) + 1.0f); // 5850: add
          r12.w = asuint(mad(asfloat(r12.w), 0.00257857703f, asfloat(r14.z))); // 5857: mad
          r10.w = asuint(asfloat(r10.w) * asfloat(r13.x)); // 5866: mul
          r10.w = asuint(asfloat(r10.w) / asfloat(r12.w)); // 5873: div
          r15.x = asuint(asfloat(r10.w) * asfloat(r11.z)); // 5880: mul
          r16.x = asuint(asfloat(r10.z) * asfloat(r10.z)); // 5887: mul
          r10.w = asuint(asfloat(r16.x) * 1.44269502f); // 5894: mul
          r10.w = asuint(exp2(asfloat(r10.w))); // 5901: exp
          r10.w = asuint(asfloat(r10.w) * 1.12837923f); // 5906: mul
          r16.y = asuint(asfloat(r16.x) * asfloat(r16.x)); // 5913: mul
          r16.zw = asuint(asfloat(r16.xy) * asfloat(r16.yy)); // 5920: mul
          r11.z = asuint(asfloat(r16.y) * asfloat(r16.z)); // 5927: mul
          r12.w = asuint(dot(float4(0.0989636481f, 0.0416374728f, 0.00473313499f, 0.00128928851f), asfloat(r16.xyzw))); // 5934: dp4
          r12.w = asuint(asfloat(r12.w) + 1.0f); // 5944: add
          r13.x = asuint(dot(float4(0.765893281f, 0.283445746f, 0.0707918629f, 0.00827315915f), asfloat(r16.xyzw))); // 5951: dp4
          r13.x = asuint(asfloat(r13.x) + 1.0f); // 5961: add
          r11.z = asuint(mad(asfloat(r11.z), 0.00257857703f, asfloat(r13.x))); // 5968: mad
          r10.z = asuint(asfloat(r10.z) * asfloat(r12.w)); // 5977: mul
          r10.z = asuint(asfloat(r10.z) / asfloat(r11.z)); // 5984: div
          r15.y = asuint(asfloat(r10.z) * asfloat(r10.w)); // 5991: mul
        } // 5998: endif
        r10.z = asuint(asfloat(r14.y) * asfloat(r1.x)); // 5999: mul
        r10.w = asuint(mad(asfloat(r11.x), asfloat(r14.x), -(asfloat(r10.z)))); // 6006: mad
        r10.w = asuint(asfloat(r10.w) * 1.7724539f); // 6016: mul
        r11.z = asuint(asfloat(r1.x) * asfloat(r1.x)); // 6023: mul
        r12.w = asuint(asfloat(r10.y) * asfloat(r11.z)); // 6030: mul
        r12.w = asuint(asfloat(r12.w) * 1.44269502f); // 6037: mul
        r12.w = asuint(exp2(asfloat(r12.w))); // 6044: exp
        r12.w = asuint(asfloat(r10.w) * asfloat(r12.w)); // 6049: mul
        r12.w = asuint(asfloat(r15.x) * asfloat(r12.w)); // 6056: mul
        r11.y = asuint(asfloat(r11.y) * asfloat(r11.y)); // 6063: mul
        r10.y = asuint(asfloat(r10.y) * asfloat(r11.y)); // 6070: mul
        r10.y = asuint(asfloat(r10.y) * 1.44269502f); // 6077: mul
        r10.y = asuint(exp2(asfloat(r10.y))); // 6084: exp
        r10.y = asuint(asfloat(r10.y) * asfloat(r10.w)); // 6089: mul
        r10.y = asuint(asfloat(r15.y) * asfloat(r10.y)); // 6096: mul
        r8.x = asuint(asfloat(r8.x) * asfloat(r9.z)); // 6103: mul
        r9.z = asuint(asfloat(r14.y) / asfloat(r11.x)); // 6110: div
        r9.y = (9.99999975e-06f < abs(asfloat(r9.y))) ? 0xffffffffu : 0u; // 6117: lt
        r10.w = asuint(-(asfloat(r8.w)) + 1.0f); // 6125: add
        r11.x = asuint(asfloat(r9.z) * asfloat(r10.w)); // 6133: mul
        r8.w = asuint(mad(asfloat(r8.w), asfloat(r10.y), -(asfloat(r12.w)))); // 6140: mad
        r8.x = asuint(asfloat(r8.w) / asfloat(r8.x)); // 6150: div
        r8.x = asuint(mad(asfloat(r9.z), asfloat(r10.w), asfloat(r8.x))); // 6157: mad
        r8.w = (asfloat(r8.w) < 1.00000001e-10f) ? 0xffffffffu : 0u; // 6166: lt
        r9.z = (9.99999997e-07f < abs(asfloat(r1.x))) ? 0xffffffffu : 0u; // 6173: lt
        r10.y = asuint(mad(asfloat(r1.x), asfloat(r14.x), asfloat(r14.y))); // 6181: mad
        r10.z = asuint(mad(asfloat(r10.z), asfloat(r6.w), asfloat(r10.y))); // 6190: mad
        r1.x = asuint(asfloat(r6.w) * -(asfloat(r1.x))); // 6199: mul
        r1.x = asuint(asfloat(r1.x) * 1.44269502f); // 6207: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 6214: exp
        r1.x = asuint(mad(-(asfloat(r10.z)), asfloat(r1.x), asfloat(r10.y))); // 6219: mad
        r1.x = asuint(asfloat(r1.x) / asfloat(r11.z)); // 6229: div
        r1.x = (r9.z != 0u) ? r1.x : r15.z; // 6236: movc
        r1.x = (r8.w != 0u) ? r11.x : r1.x; // 6245: movc
        r1.x = (r9.y != 0u) ? r8.x : r1.x; // 6254: movc
        r1.x = asuint(asfloat(r7.w) * asfloat(r1.x)); // 6263: mul
        r7.w = (1.0f < asfloat(r1.x)) ? 0xffffffffu : 0u; // 6270: lt
        r1.x = (r7.w != 0u) ? 0x3f800000u : r1.x; // 6277: movc
        r1.x = r1.x  & r9.w; // 6286: and
        r9.yzw = asuint(asfloat(r1.xxx) * float3(g_height_fog_colour.x, g_height_fog_colour.y, g_height_fog_colour.z)); // 6293: mul
        r1.x = asuint(max(asfloat(r4.x), 0.0f)); // 6301: max
        r4.x = asuint(mad(-(asfloat(r1.x)), 0.5f, asfloat(r11.w))); // 6308: mad
        r1.x = asuint(asfloat(r1.x) * asfloat(r4.x)); // 6318: mul
        r1.x = asuint(asfloat(r4.y) * asfloat(r1.x)); // 6325: mul
        r1.x = asuint(mad(asfloat(r0.z), asfloat(r1.x), asfloat(r4.w))); // 6332: mad
        r1.x = asuint(asfloat(r1.x) * -1.44269502f); // 6341: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 6348: exp
        r9.yzw = asuint(asfloat(r3.xxx) * asfloat(r9.yzw)); // 6353: mul
        r9.yzw = asuint(mad(asfloat(r12.xyz), asfloat(r1.xxx), asfloat(r9.yzw))); // 6360: mad
        r0.w = asuint(mad(asfloat(r0.x), asfloat(r1.z), asfloat(r0.w))); // 6369: mad
        r1.x = asuint(max(asfloat(r0.w), asfloat(r2.z))); // 6378: max
        r3.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 6385: add
        r3.x = asuint(asfloat(r3.x) / asfloat(r2.y)); // 6394: div
        r3.x = asuint(max(asfloat(r3.x), 0.0f)); // 6401: max
        r3.x = (r2.w != 0u) ? r3.x : 0x49742400u; // 6408: movc
        r4.x = asuint(mad(-(asfloat(r10.x)), asfloat(r6.w), asfloat(r8.z))); // 6417: mad
        r4.x = asuint(asfloat(r6.w) * asfloat(r4.x)); // 6427: mul
        r10.x = asuint(asfloat(r4.y) * asfloat(r4.x)); // 6434: mul
        r1.x = asuint(min(asfloat(r1.x), g_fog_height_top)); // 6441: min
        r1.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 6449: add
        r1.x = asuint(mad(-(asfloat(r4.z)), asfloat(r3.x), asfloat(r1.x))); // 6458: mad
        r1.x = asuint(asfloat(r3.x) * asfloat(r1.x)); // 6468: mul
        r10.y = asuint(asfloat(r4.y) * asfloat(r1.x)); // 6475: mul
        r4.xy = asuint(asfloat(r0.zz) * asfloat(r10.xy)); // 6482: mul
        r1.x = asuint(asfloat(r4.y) + asfloat(r4.x)); // 6489: add
        r3.x = asuint(asfloat(r8.y) + -(asfloat(r9.x))); // 6496: add
        r4.x = (r3.x & 0x7fffffffu); // 6504: mov
        r0.w = asuint(max(asfloat(r0.w), g_fog_height_bottom)); // 6510: max
        r0.w = asuint(-(asfloat(r0.w)) + asfloat(r2.z)); // 6518: add
        r0.w = asuint(asfloat(r0.w) / asfloat(r2.y)); // 6526: div
        r0.w = asuint(max(asfloat(r0.w), 0.0f)); // 6533: max
        r4.y = (r2.w != 0u) ? r0.w : 0x49742400u; // 6540: movc
        r4.xy = asuint(asfloat(r0.zz) * asfloat(r4.xy)); // 6549: mul
        r0.w = asuint(asfloat(r4.y) + asfloat(r4.x)); // 6556: add
        r0.w = asuint(asfloat(r0.w) + asfloat(r1.x)); // 6563: add
        r0.w = asuint(asfloat(r0.w) * -1.44269502f); // 6570: mul
        r0.w = asuint(exp2(asfloat(r0.w))); // 6577: exp
        r1.x = asuint(dot(-(asfloat(r1.yzw)), float3(sun_disk_direction.x, sun_disk_direction.y, sun_disk_direction.z))); // 6582: dp3
        r1.y = asuint(mad(asfloat(r1.x), asfloat(r1.x), 1.0f)); // 6591: mad
        r1.x = asuint(mad(-(asfloat(r1.x)), 1.51999998f, 1.5776f)); // 6600: mad
        r1.x = asuint(log2(abs(asfloat(r1.x)))); // 6610: log
        r1.xw = asuint(asfloat(r1.xy) * float2(1.5f, 0.0596831031f)); // 6616: mul
        r1.x = asuint(exp2(asfloat(r1.x))); // 6626: exp
        r1.x = asuint(asfloat(r1.y) / asfloat(r1.x)); // 6631: div
        r1.x = asuint(asfloat(r1.x) * g_sun_disc_color_scale); // 6638: mul
        r2.w = asuint(asfloat(r1.x) * 0.0195609424f); // 6646: mul
        r1.x = asuint(mad(asfloat(r1.x), 0.0195609424f, -(asfloat(r1.w)))); // 6653: mad
        r1.x = asuint(mad(g_height_fog_forward_scattering, asfloat(r1.x), asfloat(r1.w))); // 6663: mad
        r4.xy = asuint(float2(1.0f, 1.0f) / asfloat(r6.xy)); // 6673: div
        r4.zw = asuint(asfloat(r2.xx) * -(asfloat(r4.xy))); // 6683: mul
        r4.zw = asuint(asfloat(r4.zw) * float2(1.44269502f, 1.44269502f)); // 6691: mul
        r4.zw = asuint(exp2(asfloat(r4.zw))); // 6701: exp
        r6.xyw = asuint(asfloat(r4.zzz) * float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z)); // 6706: mul
        r8.xy = asuint(asfloat(r1.zz) * asfloat(r4.xy)); // 6714: mul
        r4.xy = asuint(asfloat(r2.yy) * asfloat(r4.xy)); // 6721: mul
        r8.zw = asuint(asfloat(r0.xx) * -(asfloat(r8.xy))); // 6728: mul
        r8.zw = asuint(asfloat(r8.zw) * float2(1.44269502f, 1.44269502f)); // 6736: mul
        r8.zw = asuint(exp2(asfloat(r8.zw))); // 6746: exp
        r10.xy = (float2(1.00000001e-10f, 1.00000001e-10f) < abs(asfloat(r8.xy))) ? uint2(0xffffffffu, 0xffffffffu) : uint2(0u, 0u); // 6751: lt
        r11.xyz = asuint(asfloat(r6.xyw) / asfloat(r8.xxx)); // 6762: div
        r10.zw = asuint(-(asfloat(r8.zw)) + float2(1.0f, 1.0f)); // 6769: add
        r12.xyz = asuint(asfloat(r10.zzz) * asfloat(r11.xyz)); // 6780: mul
        r12.xyz = asuint(asfloat(r12.xyz) * float3(g_fog_density_constant, g_fog_density_constant, g_fog_density_constant)); // 6787: mul
        r14.xyz = asuint(asfloat(r6.xyw) * float3(g_fog_density_constant, g_fog_density_constant, g_fog_density_constant)); // 6795: mul
        r12.xyz = (r10.xxx != uint3(0u, 0u, 0u)) ? r12.xyz : r14.xyz; // 6803: movc
        r14.xyz = asuint(asfloat(r6.xyw) / asfloat(r4.xxx)); // 6812: div
        r14.xyz = asuint(mad(asfloat(r14.xyz), asfloat(r8.zzz), asfloat(r12.xyz))); // 6819: mad
        r0.x = asuint(asfloat(r4.w) * 2.20000002e-05f); // 6828: mul
        r1.w = asuint(asfloat(r0.x) / asfloat(r8.y)); // 6835: div
        r2.x = asuint(asfloat(r10.w) * asfloat(r1.w)); // 6842: mul
        r3.x = asuint(asfloat(r2.x) * g_fog_density_constant); // 6849: mul
        r4.z = asuint(asfloat(r0.x) * g_fog_density_constant); // 6857: mul
        r3.x = (r10.y != 0u) ? r3.x : r4.z; // 6865: movc
        r0.x = asuint(asfloat(r0.x) / asfloat(r4.y)); // 6874: div
        r4.y = asuint(mad(asfloat(r0.x), asfloat(r8.w), asfloat(r3.x))); // 6881: mad
        r4.yzw = asuint(-(asfloat(r4.yyy)) + -(asfloat(r14.xyz))); // 6890: add
        r4.yzw = asuint(asfloat(r4.yzw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 6899: mul
        r4.yzw = asuint(exp2(asfloat(r4.yzw))); // 6909: exp
        r8.xzw = asuint(-(asfloat(r3.xxx)) + -(asfloat(r12.xyz))); // 6914: add
        r8.xzw = asuint(asfloat(r8.xzw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 6923: mul
        r8.xzw = asuint(exp2(asfloat(r8.xzw))); // 6933: exp
        r8.xzw = asuint(-(asfloat(r8.xzw)) + float3(1.0f, 1.0f, 1.0f)); // 6938: add
        r3.x = asuint(asfloat(r2.y) * g_fog_density_constant); // 6949: mul
        r3.x = asuint(asfloat(r1.z) / asfloat(r3.x)); // 6957: div
        r3.x = asuint(-(asfloat(r3.x)) + 1.0f); // 6964: add
        r1.z = asuint(mad(-(g_fog_density_constant), asfloat(r2.y), asfloat(r1.z))); // 6972: mad
        r10.xyw = asuint(-(asfloat(r1.www)) + -(asfloat(r11.xyz))); // 6983: add
        r10.xyw = asuint(asfloat(r10.xyw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 6992: mul
        r10.xyw = asuint(exp2(asfloat(r10.xyw))); // 7002: exp
        r11.xyz = asuint(mad(asfloat(r11.xyz), asfloat(r10.zzz), asfloat(r2.xxx))); // 7007: mad
        r10.xyz = asuint(asfloat(r10.xyw) * asfloat(r11.xyz)); // 7016: mul
        r1.w = (1.00000002e-16f < asfloat(r8.y)) ? 0xffffffffu : 0u; // 7023: lt
        r11.xyz = asuint(-(asfloat(r4.yzw)) + float3(1.0f, 1.0f, 1.0f)); // 7030: add
        r10.xyz = (r1.www != uint3(0u, 0u, 0u)) ? r10.xyz : r11.xyz; // 7041: movc
        r1.z = (abs(asfloat(r1.z)) >= 9.99999975e-06f) ? 0xffffffffu : 0u; // 7050: ge
        r6.xyw = asuint(-(asfloat(r6.xyw)) / asfloat(r4.xxx)); // 7058: div
        r6.xyw = asuint(-(asfloat(r0.xxx)) + asfloat(r6.xyw)); // 7066: add
        r6.xyw = asuint(asfloat(r6.xyw) * float3(1.44269502f, 1.44269502f, 1.44269502f)); // 7074: mul
        r6.xyw = asuint(exp2(asfloat(r6.xyw))); // 7084: exp
        r11.xyz = asuint(float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z) + float3(2.20000002e-05f, 2.20000002e-05f, 2.20000002e-05f)); // 7089: add
        r11.xyz = asuint(float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z) / asfloat(r11.xyz)); // 7100: div
        r12.xyz = (float3(1.00000001e-10f, 1.00000001e-10f, 1.00000001e-10f) < asfloat(r11.xyz)) ? uint3(0xffffffffu, 0xffffffffu, 0xffffffffu) : uint3(0u, 0u, 0u); // 7108: lt
        r11.xyz = (r12.xyz != uint3(0u, 0u, 0u)) ? r11.xyz : uint3(0x3f000000u, 0x3f000000u, 0x3f000000u); // 7118: movc
        r0.x = asuint(mad(asfloat(r1.y), 0.0596831031f, -(asfloat(r2.w)))); // 7130: mad
        r2.xyw = asuint(mad(asfloat(r11.xyz), asfloat(r0.xxx), asfloat(r2.www))); // 7140: mad
        r6.xyw = asuint(-(asfloat(r4.yzw)) + asfloat(r6.xyw)); // 7149: add
        r6.xyw = asuint(asfloat(r6.xyw) / asfloat(r3.xxx)); // 7157: div
        r1.yzw = (r1.zzz != uint3(0u, 0u, 0u)) ? r6.xyw : r10.xyz; // 7164: movc
        r1.yzw = asuint(asfloat(r1.yzw) * float3(g_fog_colour.x, g_fog_colour.y, g_fog_colour.z)); // 7173: mul
        r1.yzw = asuint(asfloat(r3.www) * asfloat(r1.yzw)); // 7181: mul
        r6.xyw = asuint(asfloat(r3.www) * asfloat(r4.yzw)); // 7188: mul
        r10.xyz = asuint(asfloat(r7.xyz) * asfloat(r1.xxx)); // 7195: mul
        r9.xyz = asuint(asfloat(r9.yzw) * float3(ambient_cube_tb[0].x, ambient_cube_tb[0].y, ambient_cube_tb[0].z)); // 7202: mul
        r9.xyz = asuint(asfloat(r9.xyz) * float3(0.0795774683f, 0.0795774683f, 0.0795774683f)); // 7210: mul
        r9.xyz = asuint(mad(asfloat(r10.xyz), asfloat(r13.yzw), asfloat(r9.xyz))); // 7220: mad
        r0.x = asuint(mad(-(asfloat(r4.y)), asfloat(r3.w), 1.0f)); // 7229: mad
        r1.x = asuint(-(asfloat(r0.w)) + asfloat(r0.x)); // 7239: add
        r1.x = asuint(asfloat(r1.x) + 1.0f); // 7247: add
        r3.x = (1.00000001e-10f < asfloat(r1.x)) ? 0xffffffffu : 0u; // 7254: lt
        r0.x = asuint(asfloat(r0.x) / asfloat(r1.x)); // 7261: div
        r0.x = (r3.x != 0u) ? r0.x : 0x3f800000u; // 7268: movc
        r4.xyz = asuint(mad(asfloat(r4.yzw), asfloat(r3.www), float3(-1.0f, -1.0f, -1.0f))); // 7277: mad
        r4.xyz = asuint(mad(asfloat(r0.xxx), asfloat(r4.xyz), float3(1.0f, 1.0f, 1.0f))); // 7289: mad
        r1.x = (0.0f < g_fog_density_height) ? 0xffffffffu : 0u; // 7301: lt
        r1.x = (r1.x != 0u) ? r2.z : asuint(g_fog_height_top); // 7309: movc
        r2.z = asuint(max(camera_position.y, g_fog_height_bottom)); // 7319: max
        r2.z = asuint(asfloat(r1.x) + -(asfloat(r2.z))); // 7328: add
        r2.z = asuint(max(asfloat(r2.z), 0.0f)); // 7336: max
        r3.x = asuint(max(asfloat(r1.x), camera_position.y)); // 7343: max
        r3.w = asuint(-(asfloat(r3.x)) + g_fog_height_top); // 7351: add
        r3.w = asuint(max(asfloat(r3.w), 0.0f)); // 7360: max
        r1.x = asuint(-(asfloat(r1.x)) + g_fog_height_top); // 7367: add
        r4.w = (0.0f != asfloat(r1.x)) ? 0xffffffffu : 0u; // 7376: ne
        r1.x = asuint(1.0f / asfloat(r1.x)); // 7386: div
        r1.x = r1.x  & r4.w; // 7396: and
        r3.x = asuint(min(asfloat(r3.x), g_fog_height_top)); // 7403: min
        r3.x = asuint(-(asfloat(r3.x)) + g_fog_height_top); // 7411: add
        r3.x = asuint(mad(-(asfloat(r3.w)), 0.5f, asfloat(r3.x))); // 7420: mad
        r3.x = asuint(asfloat(r3.w) * asfloat(r3.x)); // 7430: mul
        r1.x = asuint(asfloat(r1.x) * asfloat(r3.x)); // 7437: mul
        r1.x = asuint(asfloat(r0.z) * asfloat(r1.x)); // 7444: mul
        r0.z = asuint(mad(-(asfloat(r0.z)), asfloat(r2.z), -(asfloat(r1.x)))); // 7451: mad
        r0.z = asuint(asfloat(r0.z) * 1.44269502f); // 7462: mul
        r0.z = asuint(exp2(asfloat(r0.z))); // 7469: exp
        r3.xzw = asuint(-(asfloat(r3.zzz)) + float3(g_rayleigh_density.x, g_rayleigh_density.y, g_rayleigh_density.z)); // 7474: add
        r3.xzw = asuint(asfloat(r6.zzz) * asfloat(r3.xzw)); // 7483: mul
        r10.xyz = asuint(asfloat(r0.zzz) * float3(ambient_cube_tb[0].x, ambient_cube_tb[0].y, ambient_cube_tb[0].z)); // 7490: mul
        r8.xyz = asuint(asfloat(r8.xzw) * asfloat(r10.xyz)); // 7498: mul
        r2.xyz = asuint(asfloat(r2.xyw) * asfloat(r7.xyz)); // 7505: mul
        r1.xyz = asuint(asfloat(r1.yzw) * asfloat(r2.xyz)); // 7512: mul
        r1.xyz = asuint(mad(asfloat(r8.xyz), float3(g_fog_colour.x, g_fog_colour.y, g_fog_colour.z), asfloat(r1.xyz))); // 7519: mad
        r0.z = asuint(1.0f / asfloat(r3.y)); // 7529: div
        r0.z = asuint(-(asfloat(r0.w)) + asfloat(r0.z)); // 7539: add
        r0.x = asuint(mad(asfloat(r0.x), asfloat(r0.z), asfloat(r0.w))); // 7547: mad
        r1.xyz = asuint(asfloat(r0.xxx) * asfloat(r1.xyz)); // 7556: mul
        r0.xzw = asuint(asfloat(r0.www) * asfloat(r6.xyw)); // 7563: mul
        r0.xzw = asuint(asfloat(r3.yyy) * asfloat(r0.xzw)); // 7570: mul
        r1.xyz = asuint(mad(asfloat(r9.xyz), asfloat(r4.xyz), asfloat(r1.xyz))); // 7577: mad
        r2.xyz = asuint(asfloat(r3.xzw) * float3(-1.44269502f, -1.44269502f, -1.44269502f)); // 7586: mul
        r2.xyz = asuint(exp2(asfloat(r2.xyz))); // 7596: exp
        r1.xyz = asuint(asfloat(r1.xyz) * asfloat(r2.xyz)); // 7601: mul
        r1.xyz = asuint(asfloat(r3.yyy) * asfloat(r1.xyz)); // 7608: mul
        r1.w = r1.x  & 0x7fffffffu; // 7615: and
        r1.w = (r1.w >= 0x7f800000u) ? 0xffffffffu : 0u; // 7622: uge
        r1.xyz = (r1.www != uint3(0u, 0u, 0u)) ? uint3(0x00000000u, 0x00000000u, 0x00000000u) : r1.xyz; // 7629: movc
        r0.xzw = (r1.www != uint3(0u, 0u, 0u)) ? uint3(0x3f800000u, 0x3f800000u, 0x3f800000u) : r0.xzw; // 7641: movc
        r1.xyz = asuint(asfloat(r5.www) * asfloat(r1.xyz)); // 7653: mul
        r5.xyz = asuint(mad(asfloat(r5.xyz), asfloat(r0.xzw), asfloat(r1.xyz))); // 7660: mad
      } // 7669: endif
    } // 7670: endif
  } // 7671: endif
  r5.xyz = asuint(asfloat(r0.yyy) * asfloat(r5.xyz)); // 7672: mul
  o0.xyzw = asfloat(r5.xyzw); // 7679: mov
  return; // 7684: ret
}
