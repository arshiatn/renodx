// ---- Created with 3Dmigoto v1.3.16 on Sun Sep 20 02:33:44 2026
//
// TROY - "dof00" - GOD RAYS + GLOW COMPOSITE AND DEPTH WRITE (UAV, in place)
//
// ===========================================================================
// THIS IS NOT A DEPTH-OF-FIELD BLUR
// ===========================================================================
// It is a read-modify-write pass over the frame (RWTexture2D u0) that does two
// things, and the blur is not one of them:
//
//   1. Composites god rays and glow into the frame - the same mask, the same
//      squared mask for glow, the same glow depth masking as t01/t02/t03/t06.
//   2. Reconstructs LINEAR VIEW DEPTH from the gbuffer and writes it into the
//      frame's ALPHA channel, clamped to scene_max_distance.
//
// That explains two things at once:
//
//   - Why the DOF permutations (t04, t05, t07, t08) have no god rays and no glow
//     code: those effects move HERE when DOF is enabled. Which also means
//     SI.godrays and SI.bloom did nothing at all with DOF on until now. Fixed
//     below - this pass needed them, not those four.
//
//   - Why changing t00's alpha had no effect: this pass runs after t00 and
//     OVERWRITES alpha with real view depth. Whatever t00 put there is gone. The
//     DOF spiral in t04/t05/t07/t08 reads the alpha THIS pass wrote.
//
// ===========================================================================
// THE NEGATIVE / NaN, AND WHY IT ONLY APPEARS WITH DOF ON
// ===========================================================================
// Stock ends with
//
//     r1.w = min(scene_max_distance, view_depth);
//
// and writes it to alpha. On the stock R8G8B8A8_UNORM frame that was clamped to
// 0..1 by the ROP. The addon upgrades the frame to R16G16B16A16_FLOAT, whose
// maximum finite value is 65504 - so if scene_max_distance (or the reconstructed
// depth) exceeds that, alpha is stored as +INF.
//
// Nothing downstream cares about an infinite alpha EXCEPT the DOF spiral, which is
// the only pass that subtracts alpha from alpha:
//
//     r6 = accumulator / n          // running average, alpha = +Inf
//     r4 = -r6 + sample             // Inf - Inf = NaN   <-- here
//
// From that point the accumulator's alpha is NaN for every remaining iteration, and
// NaN reaches the frame. RGB survives the loop untouched, which is why the image
// still looks right while the analysis overlay reports a garbage minimum.
//
// It is DOF-exclusive because only the DOF loop performs that subtraction, and it is
// HDR-exclusive because SDR keeps the UNORM clamp that made the value finite.
//
// The fix is to keep depth inside the finite FP16 range at the source. 65000 is
// below the 65504 limit with margin, and is a no-op whenever scene_max_distance is
// already sane - it only ever replaces a value that was going to be Inf anyway.
//
// ===========================================================================
// DECOMPILER: THREE INSTRUCTIONS 3DMIGOTO COULD NOT EMIT
// ===========================================================================
// The dump does not compile. It left placeholder text for:
//   - dcl_uav_typed_texture2d      (the u0 declaration)
//   - resinfo on u0                (dimensions of the UAV)
//   - ld_uav_typed  / store_uav_typed
//
// All three are reconstructed below. The pixel address is the same one stock
// computed: uv * dimensions, truncated to integer, which the original built in
// r0.xw via the .xyyy broadcast.

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

cbuffer hdr_to_screen_PS : register(b2)
{
  int rain_planes_count : packoffset(c0);
  float radial_blur_strength : packoffset(c0.y);
  float2 radial_blur_position : packoffset(c0.z);
  float scene_max_distance : packoffset(c1);
  float focus_distance : packoffset(c1.y);
  float focal_length : packoffset(c1.z);
  float blur_kernel_scale : packoffset(c1.w);
  float4x4 colour_matrix : packoffset(c2);
  float4 pos_transform : packoffset(c6);
  float overscan : packoffset(c7);
  float god_rays_strength : packoffset(c7.y);
  float2 glow_strength : packoffset(c7.z);
  float glow_depth_masking : packoffset(c8);
  float disable_post_processing : packoffset(c8.y);
  float lut_strength : packoffset(c8.z);
  float3 grain_tex_offset_enabled : packoffset(c9);
}

SamplerState god_rays_sampler_s : register(s0);
SamplerState glow_sampler_s : register(s1);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> god_rays_texture : register(t1);
Texture2D<float4> glow_texture1 : register(t2);
RWTexture2D<float4> frame_texture_rw : register(u0);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


// Below the 65504 FP16 limit, with margin. See the header - this only ever replaces
// a depth that was going to be stored as +Inf.
static const float kMaxFiniteDepth = 65000.f;


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  const bool is_hdr = (HDR >= 0.5f);

  // God rays and glow are composited HERE when DOF is on, not in t04/t05/t07/t08,
  // so this is where their multipliers have to live.
  const float god_rays_multiplier = is_hdr ? SI.godrays : 1.f;
  const float glow_multiplier = is_hdr ? SI.bloom : 1.f;

  // --- manual fix: resinfo on u0 -------------------------------------------
  uint2 frame_dims;
  frame_texture_rw.GetDimensions(frame_dims.x, frame_dims.y);

  r1.xy = g_vpos_texel_offset + v0.xy;
  r1.xy = g_screen_size.zw * r1.xy;
  r1.xy = overscan * r1.xy;

  // Stock built this address in r0.xw from uv * dimensions, truncated to integer.
  int2 frame_pixel = (int2)(r1.xy * (float2)frame_dims);

  // --- manual fix: ld_uav_typed r2.xyz, r0.xwww, u0 ------------------------
  r2.xyz = frame_texture_rw[frame_pixel].xyz;

  // God rays, gated by the stock (1 - luma) mask.
  r1.zw = g_render_target_dimensions.xy * r1.xy;
  r1.zw = g_viewport_dimensions.zw * r1.zw;
  r1.z = god_rays_texture.SampleLevel(god_rays_sampler_s, r1.zw, 0).x;
  r3.xyz = sun_colour_unscaled.xyz * r1.zzz;
  r3.xyz = (god_rays_strength * god_rays_multiplier) * r3.xyz;
  r1.z = dot(r2.xyz, float3(0.330000013,0.560000002,0.109999999));
  r1.z = saturate(1 + -r1.z);
  r2.xyz = r3.xyz * r1.zzz + r2.xyz;
  r1.z = r1.z * r1.z;

  // Glow, with the stock depth mask (kills glow on anything at the far plane).
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r3.xy = uiDest.xy;
  r3.zw = (uint2)r3.xy;
  r3.xy = (int2)r3.xy + int2(-1,-1);
  r3.zw = r3.zw * r1.xy;
  r3.zw = floor(r3.zw);
  r3.zw = (int2)r3.zw;
  r3.zw = max(int2(0,0), (int2)r3.zw);
  r3.xy = min((int2)r3.zw, (int2)r3.xy);
  r3.zw = float2(0,0);
  r3.z = gbuffer_channel_4_texture.Load(r3.xyz).x;
  r1.w = -0.999989986 + r3.z;
  r1.w = saturate(100000 * r1.w);
  r1.w = -r1.w * glow_depth_masking + 1;
  r4.xyz = glow_texture1.SampleLevel(glow_sampler_s, r1.xy, 0).xyz;

  // NDC for the depth reconstruction, taken before r1.xy is overwritten.
  r3.xy = r1.xy * float2(2,-2) + float2(-1,1);

  r1.xyz = r4.xyz * r1.zzz * glow_multiplier;
  r1.xyz = r1.xyz * r1.www + r2.xyz;

  // Linear view depth -> alpha.
  r3.w = 1;
  r2.x = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
  r2.y = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);
  r2.x = r2.x / r2.y;
  r1.w = min(scene_max_distance, r2.x);

  // Keep depth finite in FP16. Without this the sky stores +Inf, and the DOF
  // spiral's "sample - running average" turns that into NaN for the whole kernel.
  r1.w = clamp(r1.w, 0.f, kMaxFiniteDepth);
  r1.w = (r1.w == r1.w) ? r1.w : kMaxFiniteDepth;

  // Colour scrub too - this pass is the last chance before the DOF blur spreads
  // anything bad across a four-pixel radius.
  r1.xyz = max(r1.xyz, 0.f.xxx);
  r1.xyz = (r1.xyz == r1.xyz) ? r1.xyz : 0.f.xxx;

  // --- manual fix: store_uav_typed u0, r0.xyzw, r1.xyzw --------------------
  frame_texture_rw[frame_pixel] = r1.xyzw;
  return;
}
