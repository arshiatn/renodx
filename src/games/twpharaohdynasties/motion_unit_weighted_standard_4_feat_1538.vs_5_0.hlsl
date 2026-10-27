// Attila unit motion (anti_aliasing.hpp): the game's 0x15389925 with its unit's placement and bones last frame (motion.hlsli); outputs SV_Position as the game's,
// this frame's and last frame's clip positions without the jitter, its world rows and bone base for
// motion_unit.ps.
// Made by tools\tw\attila_motion\make_wind_motion_vs.py.
// Translated from the disassembly by tools\tw\attila_motion\asm_to_hlsl.py (3Dmigoto's decompiler crashed on it).
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

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_untextured : packoffset(c0.y);
  float g_snow : packoffset(c0.z);
  float g_rain : packoffset(c0.w);
  float g_tesselation_factor : packoffset(c1);
  float g_tesselation_displacement_scale : packoffset(c1.y);
  float2 g_uv2_tile_interval : packoffset(c1.z);
  float2 g_texel_density_uv_scale : packoffset(c2);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
  float g_amount_blendshapes[128] : packoffset(c5);
}

cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

cbuffer cb_environment : register(b3)
{
  float2 g_wind_direction : packoffset(c0);
  float2 g_wind_vector : packoffset(c0.z);
  float2 g_wind_vector_main : packoffset(c1);
  float g_wind_gust_start_time : packoffset(c1.z);
  float g_wind_vegetation_multiplier : packoffset(c1.w);
  float g_wind_vegetation_hstrength : packoffset(c2);
  float g_wind_vegetation_vstrength : packoffset(c2.y);
  float2 g_wind_vegetation_tremble : packoffset(c2.z);
  float2 g_wind_vegetation_weaviness : packoffset(c3);
  float g_wind_vegetation_trunk_movement : packoffset(c3.z);
  float g_wind_vegetation_lerp : packoffset(c3.w);
  float2 g_wind_offset : packoffset(c4);
  float g_snowing_intensity : packoffset(c4.z);
  int g_weather_type : packoffset(c4.w);
  int g_season_type : packoffset(c5);
  float g_snow_mask : packoffset(c5.y);
  float g_creep_intensity : packoffset(c5.z);
  float g_cubemap_sun_colour_scale : packoffset(c5.w);
  float g_campaign_vegetation_tint_scale : packoffset(c6);
}

cbuffer shader_params : register(b4)
{
  float sp_Feather_Mask_Main_Gamma : packoffset(c0);
  float sp_OctA_Amplitude : packoffset(c0.y);
  float sp_OctA_Frequency : packoffset(c0.z);
  float sp_OctA_Speed : packoffset(c0.w);
  float sp_OctA_Uniformity_X : packoffset(c1);
  float sp_OctA_Uniformity_Y : packoffset(c1.y);
  float sp_OctA_Wave_Details : packoffset(c1.z);
  float sp_OctA_Wave_Details_Offset : packoffset(c1.w);
  float sp_OctX_Amplitude : packoffset(c2);
  float sp_OctX_Frequency : packoffset(c2.y);
  float sp_OctX_Speed : packoffset(c2.z);
  float sp_OctX_Uniformity_X : packoffset(c2.w);
  float sp_OctX_Uniformity_Y : packoffset(c3);
  float sp_OctX_Wave_Details_Offset : packoffset(c3.y);
  float sp_Variable_Amplitude_Max : packoffset(c3.z);
  float sp_Variable_Amplitude_Min : packoffset(c3.w);
  float sp_Variable_Speed_Max : packoffset(c4);
  float sp_Variable_Speed_Min : packoffset(c4.y);
  float sp_Wind_Inclination : packoffset(c4.z);
  float sp_Wind_Input_Magnitude_Offset : packoffset(c4.w);
  float sp_Wind_Input_Range_Max : packoffset(c5);
  float sp_Wind_Input_Range_Min : packoffset(c5.y);
}

SamplerState s_Feather_Mask_Texture : register(s0);
SamplerState s_Noise_Texture : register(s1);
SamplerState s_Noise_Texture_Horizontal : register(s2);
Texture2D<float4> t_animation_map : register(t0);
Texture2D<uint4> t_pixel_selection_object_id_texture : register(t1);
Texture2D<float4> t_Feather_Mask_Texture : register(t2);
Texture2D<float4> t_Noise_Texture : register(t3);
Texture2D<float4> t_Noise_Texture_Horizontal : register(t4);

static const uint4 icb_bits[4] = {
  uint4(1065353216u, 0u, 0u, 0u),
  uint4(0u, 1065353216u, 0u, 0u),
  uint4(0u, 0u, 1065353216u, 0u),
  uint4(0u, 0u, 0u, 1065353216u),
};

#include "./motion.hlsli"

static float motion_time;
static float4 motion_rows[4];

// The animation texture: this frame's, or (motion_previous_entry) last frame's bones from the pool, relative to
// last frame's bone base (past the pool's: this frame's at the same place relative to this frame's).
static uint motion_previous_entry;
static int motion_previous_base;
static int motion_current_base;

float4 MotionAnimationLoad(int3 coords)
{
  if (motion_previous_entry == 0u) return t_animation_map.Load(coords);
  const int width = (int)floor(0.5f + g_animation_texture_width.x);
  const int texel_index = coords.y * width + coords.x - motion_previous_base;
  if (texel_index >= 0 && texel_index < (int)BONE_TEXELS) {
    return motion_previous_bones[(motion_previous_entry - 1u) * BONE_TEXELS + (uint)texel_index];
  }
  const int current = motion_current_base + texel_index;
  return t_animation_map.Load(int3(current % width, current / width, 0));
}

void GameMain(
  float4 v0,
  float4 v1,
  float4 v2,
  float4 v3,
  float2 v4,
  float4 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float4 v13,
  float4 v14,
  uint v15,
  inout float4 o0,
  inout float4 o1,
  inout float4 o2,
  inout float4 o3,
  inout float4 o4,
  inout float4 o5,
  inout float4 o6,
  inout float4 o7,
  inout float4 o8,
  inout float4 o9,
  inout float4 o10,
  inout float4 o11,
  inout uint4 o12)
{
  float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18, r19, r20;
  o0 = 0;
  o1 = 0;
  o2 = 0;
  o3 = 0;
  o4 = 0;
  o5 = 0;
  o6 = 0;
  o7 = 0;
  o8 = 0;
  o9 = 0;
  o10 = 0;
  o11 = 0;
  o12 = 0;
  r0.xyzw = float4((v1.z * 255.00999450683594), (v1.y * 255.00999450683594), (v1.x * 255.00999450683594), (v1.w * 255.00999450683594));
  r0.xyzw = float4(floor(r0.x), floor(r0.y), floor(r0.z), floor(r0.w));
  r0.xyzw = float4(asfloat((int)r0.x), asfloat((int)r0.y), asfloat((int)r0.z), asfloat((int)r0.w));
  r1.x = (v2.y + v2.z);
  r1.x = (r1.x + v2.x);
  r1.yz = float2((v2.y + -0.0039059999398887157), (v2.x + -0.0039059999398887157));
  r1.xw = float2((-(r1.x) + 1.0), (-(r1.x) + 0.9960939884185791));
  r1.yzw = float3(saturate((r1.y * 3.4028234663852886e+38)), saturate((r1.z * 3.4028234663852886e+38)), saturate((r1.w * 3.4028234663852886e+38)));
  r2.yz = float2((r1.y * v2.y), (r1.z * v2.x));
  r2.w = (r1.w * r1.x);
  r1.xy = float2(asfloat(((0.0 != r1.y) ? 0xffffffffu : 0u)), asfloat(((0.0 != r1.z) ? 0xffffffffu : 0u)));
  r0.yz = float2(((asint(r1.x) != 0) ? r0.y : r0.x), ((asint(r1.y) != 0) ? r0.z : r0.x));
  r1.x = asfloat(((0.0 != r1.w) ? 0xffffffffu : 0u));
  r0.w = ((asint(r1.x) != 0) ? r0.w : r0.x);
  r1.x = (g_animation_texture_width.x + 0.5);
  r1.x = floor(r1.x);
  r1.z = (v10.x + 0.5);
  r1.z = floor(r1.z);
  r1.w = asfloat((asint(r0.x) * 3));
  r1.w = (float)asuint(r1.w);
  r1.w = (r1.w + r1.z);
  r1.yw = float2(asfloat((uint)r1.x), asfloat((uint)r1.w));
  {
    const uint together0_0 = (asuint(r1.w) / asuint(r1.y));
    r3.y = asfloat(together0_0);
  }
  r3.x = asfloat((-(asint(r3.y)) * asint(r1.y) + asint(r1.w)));
  r4.xy = float2(asfloat((asint(r3.x) + 1)), asfloat((asint(r3.x) + 2)));
  r3.zw = float2(asfloat(0u), asfloat(0u));
  r5.xyzw = float4(MotionAnimationLoad(int3(asint(r3.x), asint(r3.y), asint(r3.w))).x, MotionAnimationLoad(int3(asint(r3.x), asint(r3.y), asint(r3.w))).y, MotionAnimationLoad(int3(asint(r3.x), asint(r3.y), asint(r3.w))).z, MotionAnimationLoad(int3(asint(r3.x), asint(r3.y), asint(r3.w))).w);
  r4.zw = float2(r3.y, r3.w);
  r3.xyzw = float4(MotionAnimationLoad(int3(asint(r4.x), asint(r4.z), asint(r4.w))).x, MotionAnimationLoad(int3(asint(r4.x), asint(r4.z), asint(r4.w))).y, MotionAnimationLoad(int3(asint(r4.x), asint(r4.z), asint(r4.w))).z, MotionAnimationLoad(int3(asint(r4.x), asint(r4.z), asint(r4.w))).w);
  r4.xyzw = float4(MotionAnimationLoad(int3(asint(r4.y), asint(r4.z), asint(r4.w))).x, MotionAnimationLoad(int3(asint(r4.y), asint(r4.z), asint(r4.w))).y, MotionAnimationLoad(int3(asint(r4.y), asint(r4.z), asint(r4.w))).z, MotionAnimationLoad(int3(asint(r4.y), asint(r4.z), asint(r4.w))).w);
  r6.xy = float2(asfloat((asint(r0.y) * 3)), asfloat((asint(r0.z) * 3)));
  r6.xy = float2((float)asuint(r6.x), (float)asuint(r6.y));
  r6.xy = float2((r1.z + r6.x), (r1.z + r6.y));
  r6.xy = float2(asfloat((uint)r6.x), asfloat((uint)r6.y));
  {
    const uint together0_0 = (asuint(r6.x) / asuint(r1.y));
    const uint together0_1 = (asuint(r6.y) / asuint(r1.y));
    r7.xy = float2(asfloat(together0_0), asfloat(together0_1));
  }
  r6.xy = float2(asfloat((-(asint(r7.x)) * asint(r1.y) + asint(r6.x))), asfloat((-(asint(r7.y)) * asint(r1.y) + asint(r6.y))));
  r8.xyzw = float4(asfloat((asint(r6.x) + 1)), asfloat((asint(r6.x) + 2)), asfloat((asint(r6.y) + 1)), asfloat((asint(r6.y) + 2)));
  r6.z = r7.x;
  r6.w = asfloat(0u);
  r9.xyzw = float4(MotionAnimationLoad(int3(asint(r6.x), asint(r6.z), asint(r6.w))).x, MotionAnimationLoad(int3(asint(r6.x), asint(r6.z), asint(r6.w))).y, MotionAnimationLoad(int3(asint(r6.x), asint(r6.z), asint(r6.w))).z, MotionAnimationLoad(int3(asint(r6.x), asint(r6.z), asint(r6.w))).w);
  r10.xw = float2(r8.x, r8.y);
  r10.yz = float2(r6.z, r6.w);
  r11.xyzw = float4(MotionAnimationLoad(int3(asint(r10.x), asint(r10.y), asint(r10.z))).x, MotionAnimationLoad(int3(asint(r10.x), asint(r10.y), asint(r10.z))).y, MotionAnimationLoad(int3(asint(r10.x), asint(r10.y), asint(r10.z))).z, MotionAnimationLoad(int3(asint(r10.x), asint(r10.y), asint(r10.z))).w);
  r10.xyzw = float4(MotionAnimationLoad(int3(asint(r10.w), asint(r10.y), asint(r10.z))).x, MotionAnimationLoad(int3(asint(r10.w), asint(r10.y), asint(r10.z))).y, MotionAnimationLoad(int3(asint(r10.w), asint(r10.y), asint(r10.z))).z, MotionAnimationLoad(int3(asint(r10.w), asint(r10.y), asint(r10.z))).w);
  r7.zw = float2(r6.w, r6.y);
  r6.xyzw = float4(MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).x, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).y, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).z, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).w);
  r7.xw = float2(r8.z, r8.w);
  r8.xyzw = float4(MotionAnimationLoad(int3(asint(r7.x), asint(r7.y), asint(r7.z))).x, MotionAnimationLoad(int3(asint(r7.x), asint(r7.y), asint(r7.z))).y, MotionAnimationLoad(int3(asint(r7.x), asint(r7.y), asint(r7.z))).z, MotionAnimationLoad(int3(asint(r7.x), asint(r7.y), asint(r7.z))).w);
  r7.xyzw = float4(MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).x, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).y, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).z, MotionAnimationLoad(int3(asint(r7.w), asint(r7.y), asint(r7.z))).w);
  r1.w = asfloat((asint(r0.w) * 3));
  r1.w = (float)asuint(r1.w);
  r1.z = (r1.w + r1.z);
  r1.z = asfloat((uint)r1.z);
  {
    const uint together0_0 = (asuint(r1.z) / asuint(r1.y));
    r12.y = asfloat(together0_0);
  }
  r12.x = asfloat((-(asint(r12.y)) * asint(r1.y) + asint(r1.z)));
  r13.xy = float2(asfloat((asint(r12.x) + 1)), asfloat((asint(r12.x) + 2)));
  r12.zw = float2(asfloat(0u), asfloat(0u));
  r14.xyzw = float4(MotionAnimationLoad(int3(asint(r12.x), asint(r12.y), asint(r12.w))).x, MotionAnimationLoad(int3(asint(r12.x), asint(r12.y), asint(r12.w))).y, MotionAnimationLoad(int3(asint(r12.x), asint(r12.y), asint(r12.w))).z, MotionAnimationLoad(int3(asint(r12.x), asint(r12.y), asint(r12.w))).w);
  r13.zw = float2(r12.y, r12.w);
  r12.xyzw = float4(MotionAnimationLoad(int3(asint(r13.x), asint(r13.z), asint(r13.w))).x, MotionAnimationLoad(int3(asint(r13.x), asint(r13.z), asint(r13.w))).y, MotionAnimationLoad(int3(asint(r13.x), asint(r13.z), asint(r13.w))).z, MotionAnimationLoad(int3(asint(r13.x), asint(r13.z), asint(r13.w))).w);
  r13.xyzw = float4(MotionAnimationLoad(int3(asint(r13.y), asint(r13.z), asint(r13.w))).x, MotionAnimationLoad(int3(asint(r13.y), asint(r13.z), asint(r13.w))).y, MotionAnimationLoad(int3(asint(r13.y), asint(r13.z), asint(r13.w))).z, MotionAnimationLoad(int3(asint(r13.y), asint(r13.z), asint(r13.w))).w);
  r1.y = asfloat(((0.10000000149011612 < v12.x) ? 0xffffffffu : 0u));
  if (asint(r1.y) != 0) {
    r1.x = asfloat((int)r1.x);
    r1.y = (v12.x * 3.0 + v10.x);
    r1.y = asfloat((int)r1.y);
    r1.z = asfloat((asuint(r0.x) ^ 4u));
    r1.w = asfloat(max(asint(r0.x), -(asint(r0.x))));
    r15.x = asfloat((asuint(r1.w) >> (2u & 31u)));
    r15.y = asfloat((-asint(r15.x)));
    r1.z = asfloat((asuint(r1.z) & 2147483648u));
    r1.z = ((asint(r1.z) != 0) ? r15.y : r15.x);
    r1.z = asfloat((asint(r1.z) + asint(r1.y)));
    r15.x = asfloat((asuint(r1.x) ^ asuint(r1.z)));
    r15.yz = float2(asfloat(max(asint(r1.z), -(asint(r1.z)))), asfloat(max(asint(r1.x), -(asint(r1.x)))));
    {
      const uint together0_0 = (asuint(r15.y) / asuint(r15.z));
      r15.y = asfloat(together0_0);
    }
    r15.w = asfloat((-asint(r15.y)));
    r15.x = asfloat((asuint(r15.x) & 2147483648u));
    r16.y = ((asint(r15.x) != 0) ? r15.w : r15.y);
    r16.x = asfloat((-(asint(r16.y)) * asint(r1.x) + asint(r1.z)));
    r16.zw = float2(asfloat(0u), asfloat(0u));
    r16.xyzw = float4(MotionAnimationLoad(int3(asint(r16.x), asint(r16.y), asint(r16.w))).x, MotionAnimationLoad(int3(asint(r16.x), asint(r16.y), asint(r16.w))).y, MotionAnimationLoad(int3(asint(r16.x), asint(r16.y), asint(r16.w))).z, MotionAnimationLoad(int3(asint(r16.x), asint(r16.y), asint(r16.w))).w);
    r0.x = asfloat((asuint(r0.x) & 2147483648u));
    r1.z = asfloat((asuint(r1.w) & 3u));
    r1.w = asfloat((-asint(r1.z)));
    r0.x = ((asint(r0.x) != 0) ? r1.w : r1.z);
    r16.x = dot(r16.xyzw, float4(asfloat(icb_bits[(asint(r0.x) + 0)].x), asfloat(icb_bits[(asint(r0.x) + 0)].y), asfloat(icb_bits[(asint(r0.x) + 0)].z), asfloat(icb_bits[(asint(r0.x) + 0)].w)));
    r1.zw = float2(asfloat((asuint(r0.y) ^ 4u)), asfloat((asuint(r0.z) ^ 4u)));
    r15.xy = float2(asfloat(max(asint(r0.y), -(asint(r0.y)))), asfloat(max(asint(r0.z), -(asint(r0.z)))));
    r17.xy = float2(asfloat((asuint(r15.x) >> (2u & 31u))), asfloat((asuint(r15.y) >> (2u & 31u))));
    r17.zw = float2(asfloat((-asint(r17.x))), asfloat((-asint(r17.y))));
    r1.zw = float2(asfloat((asuint(r1.z) & 2147483648u)), asfloat((asuint(r1.w) & 2147483648u)));
    r1.zw = float2(((asint(r1.z) != 0) ? r17.z : r17.x), ((asint(r1.w) != 0) ? r17.w : r17.y));
    r1.zw = float2(asfloat((asint(r1.z) + asint(r1.y))), asfloat((asint(r1.w) + asint(r1.y))));
    r17.xy = float2(asfloat((asuint(r1.x) ^ asuint(r1.z))), asfloat((asuint(r1.x) ^ asuint(r1.w))));
    r17.zw = float2(asfloat(max(asint(r1.z), -(asint(r1.z)))), asfloat(max(asint(r1.w), -(asint(r1.w)))));
    {
      const uint together0_0 = (asuint(r17.z) / asuint(r15.z));
      const uint together0_1 = (asuint(r17.w) / asuint(r15.z));
      r17.zw = float2(asfloat(together0_0), asfloat(together0_1));
    }
    r18.xy = float2(asfloat((-asint(r17.z))), asfloat((-asint(r17.w))));
    r17.xy = float2(asfloat((asuint(r17.x) & 2147483648u)), asfloat((asuint(r17.y) & 2147483648u)));
    r17.xy = float2(((asint(r17.x) != 0) ? r18.x : r17.z), ((asint(r17.y) != 0) ? r18.y : r17.w));
    r18.xy = float2(asfloat((-(asint(r17.x)) * asint(r1.x) + asint(r1.z))), asfloat((-(asint(r17.y)) * asint(r1.x) + asint(r1.w))));
    r18.z = r17.x;
    r18.w = asfloat(0u);
    r19.xyzw = float4(MotionAnimationLoad(int3(asint(r18.x), asint(r18.z), asint(r18.w))).x, MotionAnimationLoad(int3(asint(r18.x), asint(r18.z), asint(r18.w))).y, MotionAnimationLoad(int3(asint(r18.x), asint(r18.z), asint(r18.w))).z, MotionAnimationLoad(int3(asint(r18.x), asint(r18.z), asint(r18.w))).w);
    r0.xy = float2(asfloat((asuint(r0.y) & 2147483648u)), asfloat((asuint(r0.z) & 2147483648u)));
    r1.zw = float2(asfloat((asuint(r15.x) & 3u)), asfloat((asuint(r15.y) & 3u)));
    r15.xy = float2(asfloat((-asint(r1.z))), asfloat((-asint(r1.w))));
    r0.xy = float2(((asint(r0.x) != 0) ? r15.x : r1.z), ((asint(r0.y) != 0) ? r15.y : r1.w));
    r16.y = dot(r19.xyzw, float4(asfloat(icb_bits[(asint(r0.x) + 0)].x), asfloat(icb_bits[(asint(r0.x) + 0)].y), asfloat(icb_bits[(asint(r0.x) + 0)].z), asfloat(icb_bits[(asint(r0.x) + 0)].w)));
    r17.zw = float2(r18.y, r18.w);
    r17.xyzw = float4(MotionAnimationLoad(int3(asint(r17.z), asint(r17.y), asint(r17.w))).x, MotionAnimationLoad(int3(asint(r17.z), asint(r17.y), asint(r17.w))).y, MotionAnimationLoad(int3(asint(r17.z), asint(r17.y), asint(r17.w))).z, MotionAnimationLoad(int3(asint(r17.z), asint(r17.y), asint(r17.w))).w);
    r16.z = dot(r17.xyzw, float4(asfloat(icb_bits[(asint(r0.y) + 0)].x), asfloat(icb_bits[(asint(r0.y) + 0)].y), asfloat(icb_bits[(asint(r0.y) + 0)].z), asfloat(icb_bits[(asint(r0.y) + 0)].w)));
    r0.x = asfloat((asuint(r0.w) ^ 4u));
    r0.y = asfloat(max(asint(r0.w), -(asint(r0.w))));
    r0.z = asfloat((asuint(r0.y) >> (2u & 31u)));
    r1.z = asfloat((-asint(r0.z)));
    r0.xy = float2(asfloat((asuint(r0.x) & 2147483648u)), asfloat((asuint(r0.y) & 3u)));
    r0.x = ((asint(r0.x) != 0) ? r1.z : r0.z);
    r0.x = asfloat((asint(r0.x) + asint(r1.y)));
    r0.z = asfloat((asuint(r1.x) ^ asuint(r0.x)));
    r1.y = asfloat(max(asint(r0.x), -(asint(r0.x))));
    {
      const uint together0_0 = (asuint(r1.y) / asuint(r15.z));
      r1.y = asfloat(together0_0);
    }
    r1.z = asfloat((-asint(r1.y)));
    r0.z = asfloat((asuint(r0.z) & 2147483648u));
    r15.y = ((asint(r0.z) != 0) ? r1.z : r1.y);
    r15.x = asfloat((-(asint(r15.y)) * asint(r1.x) + asint(r0.x)));
    r15.zw = float2(asfloat(0u), asfloat(0u));
    r1.xyzw = float4(MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).x, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).y, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).z, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).w);
    r0.x = asfloat((asuint(r0.w) & 2147483648u));
    r0.z = asfloat((-asint(r0.y)));
    r0.x = ((asint(r0.x) != 0) ? r0.z : r0.y);
    r16.w = dot(r1.xyzw, float4(asfloat(icb_bits[(asint(r0.x) + 0)].x), asfloat(icb_bits[(asint(r0.x) + 0)].y), asfloat(icb_bits[(asint(r0.x) + 0)].z), asfloat(icb_bits[(asint(r0.x) + 0)].w)));
    r0.xyzw = float4(saturate(r16.x), saturate(r16.y), saturate(r16.z), saturate(r16.w));
    r2.x = v2.z;
    r0.x = dot(r0.xyzw, r2.xyzw);
    r0.y = dot(r16.xyzw, r2.xyzw);
  } else {
    r0.xy = float2(1.0, 1.0);
  }
  r1.xyz = float3(v0.x, v0.y, v0.z);
  r1.w = 1.0;
  r15.x = dot(r1.xyzw, r5.xyzw);
  r15.y = dot(r1.xyzw, r3.xyzw);
  r15.z = dot(r1.xyzw, r4.xyzw);
  r16.xyz = float3((v3.z * 2.0 + -1.0), (v3.y * 2.0 + -1.0), (v3.x * 2.0 + -1.0));
  r2.x = dot(r16.xyz, r16.xyz);
  r2.x = rsqrt(r2.x);
  r16.xyz = float3((r2.x * r16.x), (r2.x * r16.y), (r2.x * r16.z));
  r17.x = dot(r16.xyz, r5.xyz);
  r17.y = dot(r16.xyz, r3.xyz);
  r17.z = dot(r16.xyz, r4.xyz);
  r18.xyz = float3((v5.z * 2.0 + -1.0), (v5.y * 2.0 + -1.0), (v5.x * 2.0 + -1.0));
  r2.x = dot(r18.xyz, r18.xyz);
  r2.x = rsqrt(r2.x);
  r18.xyz = float3((r2.x * r18.x), (r2.x * r18.y), (r2.x * r18.z));
  r19.x = dot(r18.xyz, r5.xyz);
  r19.y = dot(r18.xyz, r3.xyz);
  r19.z = dot(r18.xyz, r4.xyz);
  r20.xyz = float3((v6.z * 2.0 + -1.0), (v6.y * 2.0 + -1.0), (v6.x * 2.0 + -1.0));
  r2.x = dot(r20.xyz, r20.xyz);
  r2.x = rsqrt(r2.x);
  r20.xyz = float3((r2.x * r20.x), (r2.x * r20.y), (r2.x * r20.z));
  r5.x = dot(r20.xyz, r5.xyz);
  r5.y = dot(r20.xyz, r3.xyz);
  r5.z = dot(r20.xyz, r4.xyz);
  r3.x = dot(r1.xyzw, r9.xyzw);
  r3.y = dot(r1.xyzw, r11.xyzw);
  r3.z = dot(r1.xyzw, r10.xyzw);
  r3.xyz = float3((r2.y * r3.x), (r2.y * r3.y), (r2.y * r3.z));
  r3.xyz = float3((v2.z * r15.x + r3.x), (v2.z * r15.y + r3.y), (v2.z * r15.z + r3.z));
  r4.x = dot(r16.xyz, r9.xyz);
  r4.y = dot(r16.xyz, r11.xyz);
  r4.z = dot(r16.xyz, r10.xyz);
  r4.xyz = float3((r2.y * r4.x), (r2.y * r4.y), (r2.y * r4.z));
  r4.xyz = float3((v2.z * r17.x + r4.x), (v2.z * r17.y + r4.y), (v2.z * r17.z + r4.z));
  r15.x = dot(r18.xyz, r9.xyz);
  r15.y = dot(r18.xyz, r11.xyz);
  r15.z = dot(r18.xyz, r10.xyz);
  r15.xyz = float3((r2.y * r15.x), (r2.y * r15.y), (r2.y * r15.z));
  r15.xyz = float3((v2.z * r19.x + r15.x), (v2.z * r19.y + r15.y), (v2.z * r19.z + r15.z));
  r9.x = dot(r20.xyz, r9.xyz);
  r9.y = dot(r20.xyz, r11.xyz);
  r9.z = dot(r20.xyz, r10.xyz);
  r9.xyz = float3((r2.y * r9.x), (r2.y * r9.y), (r2.y * r9.z));
  r5.xyz = float3((v2.z * r5.x + r9.x), (v2.z * r5.y + r9.y), (v2.z * r5.z + r9.z));
  r9.x = dot(r1.xyzw, r6.xyzw);
  r9.y = dot(r1.xyzw, r8.xyzw);
  r9.z = dot(r1.xyzw, r7.xyzw);
  r3.xyz = float3((r2.z * r9.x + r3.x), (r2.z * r9.y + r3.y), (r2.z * r9.z + r3.z));
  r9.x = dot(r16.xyz, r6.xyz);
  r9.y = dot(r16.xyz, r8.xyz);
  r9.z = dot(r16.xyz, r7.xyz);
  r4.xyz = float3((r2.z * r9.x + r4.x), (r2.z * r9.y + r4.y), (r2.z * r9.z + r4.z));
  r9.x = dot(r18.xyz, r6.xyz);
  r9.y = dot(r18.xyz, r8.xyz);
  r9.z = dot(r18.xyz, r7.xyz);
  r9.xyz = float3((r2.z * r9.x + r15.x), (r2.z * r9.y + r15.y), (r2.z * r9.z + r15.z));
  r6.x = dot(r20.xyz, r6.xyz);
  r6.y = dot(r20.xyz, r8.xyz);
  r6.z = dot(r20.xyz, r7.xyz);
  r2.xyz = float3((r2.z * r6.x + r5.x), (r2.z * r6.y + r5.y), (r2.z * r6.z + r5.z));
  r5.x = dot(r1.xyzw, r14.xyzw);
  r5.y = dot(r1.xyzw, r12.xyzw);
  r5.z = dot(r1.xyzw, r13.xyzw);
  r1.xyz = float3((r2.w * r5.x + r3.x), (r2.w * r5.y + r3.y), (r2.w * r5.z + r3.z));
  r3.x = dot(r16.xyz, r14.xyz);
  r3.y = dot(r16.xyz, r12.xyz);
  r3.z = dot(r16.xyz, r13.xyz);
  r3.xyz = float3((r2.w * r3.x + r4.x), (r2.w * r3.y + r4.y), (r2.w * r3.z + r4.z));
  r4.x = dot(r18.xyz, r14.xyz);
  r4.y = dot(r18.xyz, r12.xyz);
  r4.z = dot(r18.xyz, r13.xyz);
  r4.xyz = float3((r2.w * r4.x + r9.x), (r2.w * r4.y + r9.y), (r2.w * r4.z + r9.z));
  r5.x = dot(r20.xyz, r14.xyz);
  r5.y = dot(r20.xyz, r12.xyz);
  r5.z = dot(r20.xyz, r13.xyz);
  r2.xyz = float3((r2.w * r5.x + r2.x), (r2.w * r5.y + r2.y), (r2.w * r5.z + r2.z));
  if (g_pixel_selection_active != 0) {
    r5.x = asfloat((asint(v15) + g_pixel_selection_start_pixel_index));
    r5.yzw = float3(asfloat(0u), asfloat(0u), asfloat(0u));
    r2.w = asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r5.x), asint(r5.y), asint(r5.w))).x);
  }
  o12 = asuint(((g_pixel_selection_active != 0) ? r2.w : asfloat(4294967295u)));
  r1.w = 1.0;
  r5.x = dot(r1.xyzw, v7.xyzw);
  r5.y = dot(r1.xyzw, v8.xyzw);
  r5.z = dot(r1.xyzw, v9.xyzw);
  r1.x = dot(r3.xyz, v7.xyz);
  r1.y = dot(r3.xyz, v8.xyz);
  r1.z = dot(r3.xyz, v9.xyz);
  r3.x = dot(r4.xyz, v7.xyz);
  r3.y = dot(r4.xyz, v8.xyz);
  r3.z = dot(r4.xyz, v9.xyz);
  r4.x = dot(r2.xyz, v7.xyz);
  r4.y = dot(r2.xyz, v8.xyz);
  r4.z = dot(r2.xyz, v9.xyz);
  r2.x = v7.x;
  r2.y = v8.x;
  r2.z = v9.x;
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = sqrt(r1.w);
  r2.xyz = float3((r2.x / r1.w), (r2.y / r1.w), (r2.z / r1.w));
  r6.x = v7.w;
  r6.y = v8.w;
  r6.z = v9.w;
  r5.xyz = float3((r5.x + -(r6.x)), (r5.y + -(r6.y)), (r5.z + -(r6.z)));
  r2.x = dot(r5.xyz, r2.xyz);
  r1.w = (r2.x / r1.w);
  r2.x = t_Feather_Mask_Texture.SampleLevel(s_Feather_Mask_Texture, float2(v4.x, v4.y), 0.0).x;
  r2.y = log2(r2.x);
  r2.y = (r2.y * sp_Feather_Mask_Main_Gamma);
  r2.y = exp2(r2.y);
  r6.x = (r2.x * sp_OctA_Wave_Details);
  r7.x = (v4.x * sp_OctX_Uniformity_X);
  r7.y = (v4.y * sp_OctX_Uniformity_Y);
  r6.yw = float2(asfloat(0u), asfloat(0u));
  r2.xz = float2((r6.x + r7.x), (r6.y + r7.y));
  r2.w = (motion_time * sp_OctX_Speed);
  r3.w = dot(g_wind_vector_main.xy, g_wind_vector_main.xy);
  r3.w = sqrt(r3.w);
  r3.w = (r3.w + sp_Wind_Input_Magnitude_Offset);
  r3.w = max(r3.w, sp_Wind_Input_Range_Min);
  r3.w = (r3.w + -(sp_Wind_Input_Range_Min));
  r4.w = (-(sp_Wind_Input_Range_Min) + sp_Wind_Input_Range_Max);
  r3.w = saturate((r3.w / r4.w));
  r3.w = log2(r3.w);
  r3.w = (r3.w * 0.6000000238418579);
  r3.w = exp2(r3.w);
  r4.w = (-(sp_Variable_Speed_Min) + sp_Variable_Speed_Max);
  r4.w = (r3.w * r4.w + sp_Variable_Speed_Min);
  r6.z = (-(r2.w) * r4.w);
  r2.xz = float2((r2.x * sp_OctX_Frequency + r6.z), (r2.z * sp_OctX_Frequency + r6.w));
  r2.x = t_Noise_Texture_Horizontal.SampleLevel(s_Noise_Texture_Horizontal, float2(r2.x, r2.z), 0.0).x;
  r2.x = (r2.x + sp_OctX_Wave_Details_Offset);
  r2.x = (r2.x + -0.5);
  r2.x = (r2.x * sp_OctX_Amplitude);
  r2.z = (-(sp_Variable_Amplitude_Min) + sp_Variable_Amplitude_Max);
  r2.z = (r3.w * r2.z + sp_Variable_Amplitude_Min);
  r2.x = (r2.z * r2.x);
  r7.x = (r2.y * r2.x + r1.w);
  r8.x = v7.y;
  r8.y = v8.y;
  r8.z = v9.y;
  r1.w = dot(r8.xyz, r8.xyz);
  r1.w = sqrt(r1.w);
  r8.xyz = float3((r8.x / r1.w), (r8.y / r1.w), (r8.z / r1.w));
  r2.x = dot(r5.xyz, r8.xyz);
  r1.w = (r2.x / r1.w);
  r2.xw = float2((sp_OctA_Uniformity_X * v4.x + r6.x), (sp_OctA_Uniformity_Y * v4.y + r6.y));
  r3.w = (motion_time * sp_OctA_Speed);
  r3.w = (r4.w * r3.w);
  r6.x = -(r3.w);
  r6.y = asfloat(0u);
  r2.xw = float2((r2.x * sp_OctA_Frequency + r6.x), (r2.w * sp_OctA_Frequency + r6.y));
  r2.x = t_Noise_Texture.SampleLevel(s_Noise_Texture, float2(r2.x, r2.w), 0.0).x;
  r2.x = (r2.x + sp_OctA_Wave_Details_Offset);
  r2.x = (r2.x + -0.5);
  r2.x = (r2.x * sp_OctA_Amplitude);
  r2.x = (r2.z * r2.x);
  r2.x = (r2.x * r2.y);
  r2.yzw = float3((sp_Wind_Inclination + -1.0), (sp_Wind_Inclination + 0.0), (sp_Wind_Inclination + -1.0));
  r2.z = dot(r2.zw, r2.zw);
  r2.z = rsqrt(r2.z);
  r2.w = (r2.z * sp_Wind_Inclination);
  r7.y = (r2.x * r2.w + r1.w);
  r6.x = v7.z;
  r6.y = v8.z;
  r6.z = v9.z;
  r1.w = dot(r6.xyz, r6.xyz);
  r1.w = sqrt(r1.w);
  r6.xyz = float3((r6.x / r1.w), (r6.y / r1.w), (r6.z / r1.w));
  r2.w = dot(r5.xyz, r6.xyz);
  r1.w = (r2.w / r1.w);
  r2.y = (r2.z * r2.y);
  r7.z = (r2.x * r2.y + r1.w);
  r7.w = 1.0;
  r2.x = dot(r7.xyzw, v7.xyzw);
  r2.y = dot(r7.xyzw, v8.xyzw);
  r2.z = dot(r7.xyzw, v9.xyzw);
  r1.w = dot(r4.xyz, r4.xyz);
  r1.w = rsqrt(r1.w);
  o4.xyz = float3((r1.w * r4.x), (r1.w * r4.y), (r1.w * r4.z));
  r1.w = dot(r3.xyz, r3.xyz);
  r1.w = rsqrt(r1.w);
  o6.xyz = float3((r1.w * r3.x), (r1.w * r3.y), (r1.w * r3.z));
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  o3.xyz = float3((r1.w * r1.x), (r1.w * r1.y), (r1.w * r1.z));
  r2.w = 1.0;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o1.xyzw = float4(r2.x, r2.y, r2.z, r2.w);
  o2.xyzw = float4(v4.x, v4.y, v4.x, v4.y);
  o3.w = v5.w;
  o4.w = v6.w;
  o5.xy = float2(v4.x, v4.y);
  o5.z = v10.y;
  o5.w = v3.w;
  o6.w = v2.w;
  o7.xy = float2(v10.z, v10.w);
  o7.zw = float2(v11.x, v11.y);
  o8.xy = float2(v11.z, v11.w);
  o8.zw = float2(v14.x, v14.y);
  r0.z = v12.y;
  r0.w = v14.z;
  o9.xyzw = float4(r0.x, r0.y, r0.z, r0.w);
  o10.xy = float2(v12.z, v12.w);
  o10.z = v14.w;
  o10.w = v0.y;
  o11.xyzw = float4(v13.x, v13.y, v13.z, v13.w);
  return;
}

void main(
  float4 v0 : POSITION0,
  float4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float2 v4 : POSITION4,
  float4 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : TEXCOORD0,
  float4 v8 : TEXCOORD1,
  float4 v9 : TEXCOORD2,
  float4 v10 : TEXCOORD3,
  float4 v11 : TEXCOORD4,
  float4 v12 : TEXCOORD5,
  float4 v13 : TEXCOORD6,
  float4 v14 : TEXCOORD7,
  uint v15 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_clip : TEXCOORD0,
  out float4 motion_previous_clip : TEXCOORD1,
  out nointerpolation float4 motion_row0 : TEXCOORD2,
  out nointerpolation float4 motion_row1 : TEXCOORD3,
  out nointerpolation float4 motion_row2 : TEXCOORD4,
  out nointerpolation float4 motion_bones : TEXCOORD5)
{
  float4 o0 = 0;
  float4 o1 = 0;
  float4 o2 = 0;
  float4 o3 = 0;
  float4 o4 = 0;
  float4 o5 = 0;
  float4 o6 = 0;
  float4 o7 = 0;
  float4 o8 = 0;
  float4 o9 = 0;
  float4 o10 = 0;
  float4 o11 = 0;
  uint4 o12 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 world = o0;
  // Last frame's placement of this mesh nearest this one, with last frame's bones when the pool has them (else the
  // current pose there).
  float4 rows[3] = {v7, v8, v9};
  float4 previous_rows[3];
  uint4 previous_bones;
  float4 previous_world = world;
  motion_time = motion_times.x;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  previous_world = o0;
  if (MotionPreviousPlacement(rows, previous_rows, previous_bones)) {
    motion_previous_entry = MotionPreviousBoneEntry(previous_bones.y);
    motion_previous_base = (int)floor(0.5f + asfloat(previous_bones.z));
    motion_current_base = (int)floor(0.5f + v10.x);
    const float4 previous_base_input = float4((motion_previous_entry != 0u ? asfloat(previous_bones.z) : v10.x), v10.yzw);
    motion_time = motion_times.x;  // last frame's (fur and the like sway with it)
    GameMain(v0, v1, v2, v3, v4, v5, v6, previous_rows[0], previous_rows[1], previous_rows[2], previous_base_input, v11, v12, v13, v14, v15, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
    previous_world = o0;
    motion_previous_entry = 0u;
  }
  motion_row0 = v7;
  motion_row1 = v8;
  motion_row2 = v9;
  motion_bones = float4(v10.x, g_animation_texture_width.x, 0.f, 0.f);
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
