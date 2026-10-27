// Attila unit motion (anti_aliasing.hpp): the game's 0xF81881EB with its unit's placement and bones last frame (motion.hlsli); outputs SV_Position as the game's,
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

Texture2D<float4> t_animation_map : register(t0);
Texture2D<uint4> t_pixel_selection_object_id_texture : register(t1);

uint Ubfe(uint width, uint offset, uint value)
{
  width &= 31u;
  offset &= 31u;
  if (width == 0u) return 0u;
  return (width + offset < 32u) ? (value << (32u - width - offset)) >> (32u - width) : value >> offset;
}

int Ibfe(uint width, uint offset, int value)
{
  width &= 31u;
  offset &= 31u;
  if (width == 0u) return 0;
  return (width + offset < 32u) ? (value << (32u - width - offset)) >> (32u - width) : value >> offset;
}

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
  uint4 v1,
  float4 v2,
  float4 v3,
  float4 v4,
  float2 v5,
  float4 v6,
  float4 v7,
  float4 v8,
  float4 v9,
  float4 v10,
  float4 v11,
  float4 v12,
  float4 v13,
  float4 v14,
  float4 v15,
  uint v16,
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
  float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31, r32;
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
  r0.xyzw = float4(asfloat((v1.x & 255u)), asfloat((v1.y & 255u)), asfloat((v1.z & 255u)), asfloat((v1.w & 255u)));
  r1.xyzw = float4(asfloat(Ubfe(8u, 8u, v1.x)), asfloat(Ubfe(8u, 8u, v1.y)), asfloat(Ubfe(8u, 8u, v1.z)), asfloat(Ubfe(8u, 8u, v1.w)));
  r2.x = (v2.x + v2.w);
  r2.x = (r2.x + v2.y);
  r2.x = (r2.x + v2.z);
  r2.x = (r2.x + v3.x);
  r2.x = (r2.x + v3.y);
  r2.x = (r2.x + v3.z);
  r2.yzw = float3((v2.x + -0.0039059999398887157), (v2.y + -0.0039059999398887157), (v2.z + -0.0039059999398887157));
  r2.yzw = float3(saturate((r2.y * 3.4028234663852886e+38)), saturate((r2.z * 3.4028234663852886e+38)), saturate((r2.w * 3.4028234663852886e+38)));
  r3.xyz = float3((v3.x + -0.0039059999398887157), (v3.y + -0.0039059999398887157), (v3.z + -0.0039059999398887157));
  r3.xyz = float3(saturate((r3.x * 3.4028234663852886e+38)), saturate((r3.y * 3.4028234663852886e+38)), saturate((r3.z * 3.4028234663852886e+38)));
  r4.xy = float2((-(r2.x) + 1.0), (-(r2.x) + 0.9960939884185791));
  r2.x = saturate((r4.y * 3.4028234663852886e+38));
  r4.yzw = float3((r2.y * v2.x), (r2.z * v2.y), (r2.w * v2.z));
  r5.xyz = float3((r3.x * v3.x), (r3.y * v3.y), (r3.z * v3.z));
  r3.w = (r2.x * r4.x);
  r2.xyzw = float4(asfloat(((0.0 != r2.x) ? 0xffffffffu : 0u)), asfloat(((0.0 != r2.y) ? 0xffffffffu : 0u)), asfloat(((0.0 != r2.z) ? 0xffffffffu : 0u)), asfloat(((0.0 != r2.w) ? 0xffffffffu : 0u)));
  r1.x = ((asint(r2.z) != 0) ? r1.x : r1.y);
  r0.xy = float2(((asint(r2.y) != 0) ? r0.y : r1.y), ((asint(r2.w) != 0) ? r0.x : r1.y));
  r2.yzw = float3(asfloat(((0.0 != r3.x) ? 0xffffffffu : 0u)), asfloat(((0.0 != r3.y) ? 0xffffffffu : 0u)), asfloat(((0.0 != r3.z) ? 0xffffffffu : 0u)));
  r0.zw = float2(((asint(r2.y) != 0) ? r0.w : r1.y), ((asint(r2.w) != 0) ? r0.z : r1.y));
  r1.zw = float2(((asint(r2.z) != 0) ? r1.z : r1.y), ((asint(r2.x) != 0) ? r1.w : r1.y));
  r2.x = (g_animation_texture_width.x + 0.5);
  r2.x = floor(r2.x);
  r2.x = asfloat((uint)r2.x);
  r2.y = (v11.x + 0.5);
  r2.y = floor(r2.y);
  r1.y = asfloat((asint(r1.y) * 3));
  r1.y = (float)asuint(r1.y);
  r1.y = (r1.y + r2.y);
  r1.y = asfloat((uint)r1.y);
  {
    const uint together0_0 = (asuint(r1.y) / asuint(r2.x));
    r6.y = asfloat(together0_0);
  }
  r6.x = asfloat((-(asint(r6.y)) * asint(r2.x) + asint(r1.y)));
  r7.xy = float2(asfloat((asint(r6.x) + 1)), asfloat((asint(r6.x) + 2)));
  r6.zw = float2(asfloat(0u), asfloat(0u));
  r8.xyzw = float4(MotionAnimationLoad(int3(asint(r6.x), asint(r6.y), asint(r6.w))).x, MotionAnimationLoad(int3(asint(r6.x), asint(r6.y), asint(r6.w))).y, MotionAnimationLoad(int3(asint(r6.x), asint(r6.y), asint(r6.w))).z, MotionAnimationLoad(int3(asint(r6.x), asint(r6.y), asint(r6.w))).w);
  r7.zw = float2(r6.y, r6.w);
  r6.xyzw = float4(MotionAnimationLoad(int3(asint(r7.x), asint(r7.z), asint(r7.w))).x, MotionAnimationLoad(int3(asint(r7.x), asint(r7.z), asint(r7.w))).y, MotionAnimationLoad(int3(asint(r7.x), asint(r7.z), asint(r7.w))).z, MotionAnimationLoad(int3(asint(r7.x), asint(r7.z), asint(r7.w))).w);
  r7.xyzw = float4(MotionAnimationLoad(int3(asint(r7.y), asint(r7.z), asint(r7.w))).x, MotionAnimationLoad(int3(asint(r7.y), asint(r7.z), asint(r7.w))).y, MotionAnimationLoad(int3(asint(r7.y), asint(r7.z), asint(r7.w))).z, MotionAnimationLoad(int3(asint(r7.y), asint(r7.z), asint(r7.w))).w);
  r0.xy = float2(asfloat((asint(r0.x) * 3)), asfloat((asint(r0.y) * 3)));
  r0.xy = float2((float)asuint(r0.x), (float)asuint(r0.y));
  r0.xy = float2((r0.x + r2.y), (r0.y + r2.y));
  r0.xy = float2(asfloat((uint)r0.x), asfloat((uint)r0.y));
  {
    const uint together0_0 = (asuint(r0.x) / asuint(r2.x));
    const uint together0_1 = (asuint(r0.y) / asuint(r2.x));
    r9.xy = float2(asfloat(together0_0), asfloat(together0_1));
  }
  r10.xy = float2(asfloat((-(asint(r9.x)) * asint(r2.x) + asint(r0.x))), asfloat((-(asint(r9.y)) * asint(r2.x) + asint(r0.y))));
  r11.xyzw = float4(asfloat((asint(r10.x) + 1)), asfloat((asint(r10.x) + 2)), asfloat((asint(r10.y) + 1)), asfloat((asint(r10.y) + 2)));
  r10.z = r9.x;
  r10.w = asfloat(0u);
  r12.xyzw = float4(MotionAnimationLoad(int3(asint(r10.x), asint(r10.z), asint(r10.w))).x, MotionAnimationLoad(int3(asint(r10.x), asint(r10.z), asint(r10.w))).y, MotionAnimationLoad(int3(asint(r10.x), asint(r10.z), asint(r10.w))).z, MotionAnimationLoad(int3(asint(r10.x), asint(r10.z), asint(r10.w))).w);
  r13.xw = float2(r11.x, r11.y);
  r13.yz = float2(r10.z, r10.w);
  r14.xyzw = float4(MotionAnimationLoad(int3(asint(r13.x), asint(r13.y), asint(r13.z))).x, MotionAnimationLoad(int3(asint(r13.x), asint(r13.y), asint(r13.z))).y, MotionAnimationLoad(int3(asint(r13.x), asint(r13.y), asint(r13.z))).z, MotionAnimationLoad(int3(asint(r13.x), asint(r13.y), asint(r13.z))).w);
  r13.xyzw = float4(MotionAnimationLoad(int3(asint(r13.w), asint(r13.y), asint(r13.z))).x, MotionAnimationLoad(int3(asint(r13.w), asint(r13.y), asint(r13.z))).y, MotionAnimationLoad(int3(asint(r13.w), asint(r13.y), asint(r13.z))).z, MotionAnimationLoad(int3(asint(r13.w), asint(r13.y), asint(r13.z))).w);
  r0.x = asfloat((asint(r1.x) * 3));
  r0.x = (float)asuint(r0.x);
  r0.x = (r0.x + r2.y);
  r0.x = asfloat((uint)r0.x);
  {
    const uint together0_0 = (asuint(r0.x) / asuint(r2.x));
    r15.y = asfloat(together0_0);
  }
  r15.x = asfloat((-(asint(r15.y)) * asint(r2.x) + asint(r0.x)));
  r16.xy = float2(asfloat((asint(r15.x) + 1)), asfloat((asint(r15.x) + 2)));
  r15.zw = float2(asfloat(0u), asfloat(0u));
  r17.xyzw = float4(MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).x, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).y, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).z, MotionAnimationLoad(int3(asint(r15.x), asint(r15.y), asint(r15.w))).w);
  r16.zw = float2(r15.y, r15.w);
  r15.xyzw = float4(MotionAnimationLoad(int3(asint(r16.x), asint(r16.z), asint(r16.w))).x, MotionAnimationLoad(int3(asint(r16.x), asint(r16.z), asint(r16.w))).y, MotionAnimationLoad(int3(asint(r16.x), asint(r16.z), asint(r16.w))).z, MotionAnimationLoad(int3(asint(r16.x), asint(r16.z), asint(r16.w))).w);
  r16.xyzw = float4(MotionAnimationLoad(int3(asint(r16.y), asint(r16.z), asint(r16.w))).x, MotionAnimationLoad(int3(asint(r16.y), asint(r16.z), asint(r16.w))).y, MotionAnimationLoad(int3(asint(r16.y), asint(r16.z), asint(r16.w))).z, MotionAnimationLoad(int3(asint(r16.y), asint(r16.z), asint(r16.w))).w);
  r9.zw = float2(r10.w, r10.y);
  r10.xyzw = float4(MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).x, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).y, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).z, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).w);
  r9.xw = float2(r11.z, r11.w);
  r11.xyzw = float4(MotionAnimationLoad(int3(asint(r9.x), asint(r9.y), asint(r9.z))).x, MotionAnimationLoad(int3(asint(r9.x), asint(r9.y), asint(r9.z))).y, MotionAnimationLoad(int3(asint(r9.x), asint(r9.y), asint(r9.z))).z, MotionAnimationLoad(int3(asint(r9.x), asint(r9.y), asint(r9.z))).w);
  r9.xyzw = float4(MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).x, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).y, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).z, MotionAnimationLoad(int3(asint(r9.w), asint(r9.y), asint(r9.z))).w);
  r0.xy = float2(asfloat((asint(r0.z) * 3)), asfloat((asint(r0.w) * 3)));
  r0.xy = float2((float)asuint(r0.x), (float)asuint(r0.y));
  r0.xy = float2((r0.x + r2.y), (r0.y + r2.y));
  r0.xy = float2(asfloat((uint)r0.x), asfloat((uint)r0.y));
  {
    const uint together0_0 = (asuint(r0.x) / asuint(r2.x));
    const uint together0_1 = (asuint(r0.y) / asuint(r2.x));
    r18.xy = float2(asfloat(together0_0), asfloat(together0_1));
  }
  r0.xy = float2(asfloat((-(asint(r18.x)) * asint(r2.x) + asint(r0.x))), asfloat((-(asint(r18.y)) * asint(r2.x) + asint(r0.y))));
  r19.xyzw = float4(asfloat((asint(r0.x) + 1)), asfloat((asint(r0.x) + 2)), asfloat((asint(r0.y) + 1)), asfloat((asint(r0.y) + 2)));
  r0.z = r18.x;
  r0.w = asfloat(0u);
  r20.xyzw = float4(MotionAnimationLoad(int3(asint(r0.x), asint(r0.z), asint(r0.w))).x, MotionAnimationLoad(int3(asint(r0.x), asint(r0.z), asint(r0.w))).y, MotionAnimationLoad(int3(asint(r0.x), asint(r0.z), asint(r0.w))).z, MotionAnimationLoad(int3(asint(r0.x), asint(r0.z), asint(r0.w))).w);
  r21.xw = float2(r19.x, r19.y);
  r21.yz = float2(r0.z, r0.w);
  r22.xyzw = float4(MotionAnimationLoad(int3(asint(r21.x), asint(r21.y), asint(r21.z))).x, MotionAnimationLoad(int3(asint(r21.x), asint(r21.y), asint(r21.z))).y, MotionAnimationLoad(int3(asint(r21.x), asint(r21.y), asint(r21.z))).z, MotionAnimationLoad(int3(asint(r21.x), asint(r21.y), asint(r21.z))).w);
  r21.xyzw = float4(MotionAnimationLoad(int3(asint(r21.w), asint(r21.y), asint(r21.z))).x, MotionAnimationLoad(int3(asint(r21.w), asint(r21.y), asint(r21.z))).y, MotionAnimationLoad(int3(asint(r21.w), asint(r21.y), asint(r21.z))).z, MotionAnimationLoad(int3(asint(r21.w), asint(r21.y), asint(r21.z))).w);
  r0.x = asfloat((asint(r1.z) * 3));
  r0.x = (float)asuint(r0.x);
  r0.x = (r0.x + r2.y);
  r0.x = asfloat((uint)r0.x);
  {
    const uint together0_0 = (asuint(r0.x) / asuint(r2.x));
    r23.y = asfloat(together0_0);
  }
  r23.x = asfloat((-(asint(r23.y)) * asint(r2.x) + asint(r0.x)));
  r24.xy = float2(asfloat((asint(r23.x) + 1)), asfloat((asint(r23.x) + 2)));
  r23.zw = float2(asfloat(0u), asfloat(0u));
  r25.xyzw = float4(MotionAnimationLoad(int3(asint(r23.x), asint(r23.y), asint(r23.w))).x, MotionAnimationLoad(int3(asint(r23.x), asint(r23.y), asint(r23.w))).y, MotionAnimationLoad(int3(asint(r23.x), asint(r23.y), asint(r23.w))).z, MotionAnimationLoad(int3(asint(r23.x), asint(r23.y), asint(r23.w))).w);
  r24.zw = float2(r23.y, r23.w);
  r23.xyzw = float4(MotionAnimationLoad(int3(asint(r24.x), asint(r24.z), asint(r24.w))).x, MotionAnimationLoad(int3(asint(r24.x), asint(r24.z), asint(r24.w))).y, MotionAnimationLoad(int3(asint(r24.x), asint(r24.z), asint(r24.w))).z, MotionAnimationLoad(int3(asint(r24.x), asint(r24.z), asint(r24.w))).w);
  r24.xyzw = float4(MotionAnimationLoad(int3(asint(r24.y), asint(r24.z), asint(r24.w))).x, MotionAnimationLoad(int3(asint(r24.y), asint(r24.z), asint(r24.w))).y, MotionAnimationLoad(int3(asint(r24.y), asint(r24.z), asint(r24.w))).z, MotionAnimationLoad(int3(asint(r24.y), asint(r24.z), asint(r24.w))).w);
  r18.zw = float2(r0.w, r0.y);
  r0.xyzw = float4(MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).x, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).y, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).z, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).w);
  r18.xw = float2(r19.z, r19.w);
  r19.xyzw = float4(MotionAnimationLoad(int3(asint(r18.x), asint(r18.y), asint(r18.z))).x, MotionAnimationLoad(int3(asint(r18.x), asint(r18.y), asint(r18.z))).y, MotionAnimationLoad(int3(asint(r18.x), asint(r18.y), asint(r18.z))).z, MotionAnimationLoad(int3(asint(r18.x), asint(r18.y), asint(r18.z))).w);
  r18.xyzw = float4(MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).x, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).y, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).z, MotionAnimationLoad(int3(asint(r18.w), asint(r18.y), asint(r18.z))).w);
  r1.x = asfloat((asint(r1.w) * 3));
  r1.x = (float)asuint(r1.x);
  r1.x = (r1.x + r2.y);
  r1.x = asfloat((uint)r1.x);
  {
    const uint together0_0 = (asuint(r1.x) / asuint(r2.x));
    r26.y = asfloat(together0_0);
  }
  r26.x = asfloat((-(asint(r26.y)) * asint(r2.x) + asint(r1.x)));
  r1.xy = float2(asfloat((asint(r26.x) + 1)), asfloat((asint(r26.x) + 2)));
  r26.zw = float2(asfloat(0u), asfloat(0u));
  r2.xyzw = float4(MotionAnimationLoad(int3(asint(r26.x), asint(r26.y), asint(r26.w))).x, MotionAnimationLoad(int3(asint(r26.x), asint(r26.y), asint(r26.w))).y, MotionAnimationLoad(int3(asint(r26.x), asint(r26.y), asint(r26.w))).z, MotionAnimationLoad(int3(asint(r26.x), asint(r26.y), asint(r26.w))).w);
  r1.zw = float2(r26.y, r26.w);
  r26.xyzw = float4(MotionAnimationLoad(int3(asint(r1.x), asint(r1.z), asint(r1.w))).x, MotionAnimationLoad(int3(asint(r1.x), asint(r1.z), asint(r1.w))).y, MotionAnimationLoad(int3(asint(r1.x), asint(r1.z), asint(r1.w))).z, MotionAnimationLoad(int3(asint(r1.x), asint(r1.z), asint(r1.w))).w);
  r1.xyzw = float4(MotionAnimationLoad(int3(asint(r1.y), asint(r1.z), asint(r1.w))).x, MotionAnimationLoad(int3(asint(r1.y), asint(r1.z), asint(r1.w))).y, MotionAnimationLoad(int3(asint(r1.y), asint(r1.z), asint(r1.w))).z, MotionAnimationLoad(int3(asint(r1.y), asint(r1.z), asint(r1.w))).w);
  r27.xyz = float3(v0.x, v0.y, v0.z);
  r27.w = 1.0;
  r3.x = dot(r27.xyzw, r8.xyzw);
  r3.y = dot(r27.xyzw, r6.xyzw);
  r3.z = dot(r27.xyzw, r7.xyzw);
  r28.xyz = float3((v4.z * 2.0 + -1.0), (v4.y * 2.0 + -1.0), (v4.x * 2.0 + -1.0));
  r4.x = dot(r28.xyz, r28.xyz);
  r4.x = rsqrt(r4.x);
  r28.xyz = float3((r4.x * r28.x), (r4.x * r28.y), (r4.x * r28.z));
  r29.x = dot(r28.xyz, r8.xyz);
  r29.y = dot(r28.xyz, r6.xyz);
  r29.z = dot(r28.xyz, r7.xyz);
  r30.xyz = float3((v6.z * 2.0 + -1.0), (v6.y * 2.0 + -1.0), (v6.x * 2.0 + -1.0));
  r4.x = dot(r30.xyz, r30.xyz);
  r4.x = rsqrt(r4.x);
  r30.xyz = float3((r4.x * r30.x), (r4.x * r30.y), (r4.x * r30.z));
  r31.x = dot(r30.xyz, r8.xyz);
  r31.y = dot(r30.xyz, r6.xyz);
  r31.z = dot(r30.xyz, r7.xyz);
  r32.xyz = float3((v7.z * 2.0 + -1.0), (v7.y * 2.0 + -1.0), (v7.x * 2.0 + -1.0));
  r4.x = dot(r32.xyz, r32.xyz);
  r4.x = rsqrt(r4.x);
  r32.xyz = float3((r4.x * r32.x), (r4.x * r32.y), (r4.x * r32.z));
  r8.x = dot(r32.xyz, r8.xyz);
  r8.y = dot(r32.xyz, r6.xyz);
  r8.z = dot(r32.xyz, r7.xyz);
  r6.x = dot(r27.xyzw, r12.xyzw);
  r6.y = dot(r27.xyzw, r14.xyzw);
  r6.z = dot(r27.xyzw, r13.xyzw);
  r6.xyz = float3((r4.y * r6.x), (r4.y * r6.y), (r4.y * r6.z));
  r3.xyz = float3((v2.w * r3.x + r6.x), (v2.w * r3.y + r6.y), (v2.w * r3.z + r6.z));
  r6.x = dot(r28.xyz, r12.xyz);
  r6.y = dot(r28.xyz, r14.xyz);
  r6.z = dot(r28.xyz, r13.xyz);
  r6.xyz = float3((r4.y * r6.x), (r4.y * r6.y), (r4.y * r6.z));
  r6.xyz = float3((v2.w * r29.x + r6.x), (v2.w * r29.y + r6.y), (v2.w * r29.z + r6.z));
  r7.x = dot(r30.xyz, r12.xyz);
  r7.y = dot(r30.xyz, r14.xyz);
  r7.z = dot(r30.xyz, r13.xyz);
  r7.xyz = float3((r4.y * r7.x), (r4.y * r7.y), (r4.y * r7.z));
  r7.xyz = float3((v2.w * r31.x + r7.x), (v2.w * r31.y + r7.y), (v2.w * r31.z + r7.z));
  r12.x = dot(r32.xyz, r12.xyz);
  r12.y = dot(r32.xyz, r14.xyz);
  r12.z = dot(r32.xyz, r13.xyz);
  r12.xyz = float3((r4.y * r12.x), (r4.y * r12.y), (r4.y * r12.z));
  r8.xyz = float3((v2.w * r8.x + r12.x), (v2.w * r8.y + r12.y), (v2.w * r8.z + r12.z));
  r12.x = dot(r27.xyzw, r17.xyzw);
  r12.y = dot(r27.xyzw, r15.xyzw);
  r12.z = dot(r27.xyzw, r16.xyzw);
  r3.xyz = float3((r4.z * r12.x + r3.x), (r4.z * r12.y + r3.y), (r4.z * r12.z + r3.z));
  r12.x = dot(r28.xyz, r17.xyz);
  r12.y = dot(r28.xyz, r15.xyz);
  r12.z = dot(r28.xyz, r16.xyz);
  r6.xyz = float3((r4.z * r12.x + r6.x), (r4.z * r12.y + r6.y), (r4.z * r12.z + r6.z));
  r12.x = dot(r30.xyz, r17.xyz);
  r12.y = dot(r30.xyz, r15.xyz);
  r12.z = dot(r30.xyz, r16.xyz);
  r7.xyz = float3((r4.z * r12.x + r7.x), (r4.z * r12.y + r7.y), (r4.z * r12.z + r7.z));
  r12.x = dot(r32.xyz, r17.xyz);
  r12.y = dot(r32.xyz, r15.xyz);
  r12.z = dot(r32.xyz, r16.xyz);
  r4.xyz = float3((r4.z * r12.x + r8.x), (r4.z * r12.y + r8.y), (r4.z * r12.z + r8.z));
  r8.x = dot(r27.xyzw, r10.xyzw);
  r8.y = dot(r27.xyzw, r11.xyzw);
  r8.z = dot(r27.xyzw, r9.xyzw);
  r3.xyz = float3((r4.w * r8.x + r3.x), (r4.w * r8.y + r3.y), (r4.w * r8.z + r3.z));
  r8.x = dot(r28.xyz, r10.xyz);
  r8.y = dot(r28.xyz, r11.xyz);
  r8.z = dot(r28.xyz, r9.xyz);
  r6.xyz = float3((r4.w * r8.x + r6.x), (r4.w * r8.y + r6.y), (r4.w * r8.z + r6.z));
  r8.x = dot(r30.xyz, r10.xyz);
  r8.y = dot(r30.xyz, r11.xyz);
  r8.z = dot(r30.xyz, r9.xyz);
  r7.xyz = float3((r4.w * r8.x + r7.x), (r4.w * r8.y + r7.y), (r4.w * r8.z + r7.z));
  r8.x = dot(r32.xyz, r10.xyz);
  r8.y = dot(r32.xyz, r11.xyz);
  r8.z = dot(r32.xyz, r9.xyz);
  r4.xyz = float3((r4.w * r8.x + r4.x), (r4.w * r8.y + r4.y), (r4.w * r8.z + r4.z));
  r8.x = dot(r27.xyzw, r20.xyzw);
  r8.y = dot(r27.xyzw, r22.xyzw);
  r8.z = dot(r27.xyzw, r21.xyzw);
  r3.xyz = float3((r5.x * r8.x + r3.x), (r5.x * r8.y + r3.y), (r5.x * r8.z + r3.z));
  r8.x = dot(r28.xyz, r20.xyz);
  r8.y = dot(r28.xyz, r22.xyz);
  r8.z = dot(r28.xyz, r21.xyz);
  r6.xyz = float3((r5.x * r8.x + r6.x), (r5.x * r8.y + r6.y), (r5.x * r8.z + r6.z));
  r8.x = dot(r30.xyz, r20.xyz);
  r8.y = dot(r30.xyz, r22.xyz);
  r8.z = dot(r30.xyz, r21.xyz);
  r7.xyz = float3((r5.x * r8.x + r7.x), (r5.x * r8.y + r7.y), (r5.x * r8.z + r7.z));
  r8.x = dot(r32.xyz, r20.xyz);
  r8.y = dot(r32.xyz, r22.xyz);
  r8.z = dot(r32.xyz, r21.xyz);
  r4.xyz = float3((r5.x * r8.x + r4.x), (r5.x * r8.y + r4.y), (r5.x * r8.z + r4.z));
  r8.x = dot(r27.xyzw, r25.xyzw);
  r8.y = dot(r27.xyzw, r23.xyzw);
  r8.z = dot(r27.xyzw, r24.xyzw);
  r3.xyz = float3((r5.y * r8.x + r3.x), (r5.y * r8.y + r3.y), (r5.y * r8.z + r3.z));
  r8.x = dot(r28.xyz, r25.xyz);
  r8.y = dot(r28.xyz, r23.xyz);
  r8.z = dot(r28.xyz, r24.xyz);
  r6.xyz = float3((r5.y * r8.x + r6.x), (r5.y * r8.y + r6.y), (r5.y * r8.z + r6.z));
  r8.x = dot(r30.xyz, r25.xyz);
  r8.y = dot(r30.xyz, r23.xyz);
  r8.z = dot(r30.xyz, r24.xyz);
  r7.xyz = float3((r5.y * r8.x + r7.x), (r5.y * r8.y + r7.y), (r5.y * r8.z + r7.z));
  r8.x = dot(r32.xyz, r25.xyz);
  r8.y = dot(r32.xyz, r23.xyz);
  r8.z = dot(r32.xyz, r24.xyz);
  r4.xyz = float3((r5.y * r8.x + r4.x), (r5.y * r8.y + r4.y), (r5.y * r8.z + r4.z));
  r8.x = dot(r27.xyzw, r0.xyzw);
  r8.y = dot(r27.xyzw, r19.xyzw);
  r8.z = dot(r27.xyzw, r18.xyzw);
  r3.xyz = float3((r5.z * r8.x + r3.x), (r5.z * r8.y + r3.y), (r5.z * r8.z + r3.z));
  r8.x = dot(r28.xyz, r0.xyz);
  r8.y = dot(r28.xyz, r19.xyz);
  r8.z = dot(r28.xyz, r18.xyz);
  r5.xyw = float3((r5.z * r8.x + r6.x), (r5.z * r8.y + r6.y), (r5.z * r8.z + r6.z));
  r6.x = dot(r30.xyz, r0.xyz);
  r6.y = dot(r30.xyz, r19.xyz);
  r6.z = dot(r30.xyz, r18.xyz);
  r6.xyz = float3((r5.z * r6.x + r7.x), (r5.z * r6.y + r7.y), (r5.z * r6.z + r7.z));
  r0.x = dot(r32.xyz, r0.xyz);
  r0.y = dot(r32.xyz, r19.xyz);
  r0.z = dot(r32.xyz, r18.xyz);
  r0.xyz = float3((r5.z * r0.x + r4.x), (r5.z * r0.y + r4.y), (r5.z * r0.z + r4.z));
  r4.x = dot(r27.xyzw, r2.xyzw);
  r4.y = dot(r27.xyzw, r26.xyzw);
  r4.z = dot(r27.xyzw, r1.xyzw);
  r4.xyz = float3((r3.w * r4.x + r3.x), (r3.w * r4.y + r3.y), (r3.w * r4.z + r3.z));
  r3.x = dot(r28.xyz, r2.xyz);
  r3.y = dot(r28.xyz, r26.xyz);
  r3.z = dot(r28.xyz, r1.xyz);
  r3.xyz = float3((r3.w * r3.x + r5.x), (r3.w * r3.y + r5.y), (r3.w * r3.z + r5.w));
  r5.x = dot(r30.xyz, r2.xyz);
  r5.y = dot(r30.xyz, r26.xyz);
  r5.z = dot(r30.xyz, r1.xyz);
  r5.xyz = float3((r3.w * r5.x + r6.x), (r3.w * r5.y + r6.y), (r3.w * r5.z + r6.z));
  r2.x = dot(r32.xyz, r2.xyz);
  r2.y = dot(r32.xyz, r26.xyz);
  r2.z = dot(r32.xyz, r1.xyz);
  r0.xyz = float3((r3.w * r2.x + r0.x), (r3.w * r2.y + r0.y), (r3.w * r2.z + r0.z));
  if (g_pixel_selection_active != 0) {
    r1.x = asfloat((asint(v16) + g_pixel_selection_start_pixel_index));
    r1.yzw = float3(asfloat(0u), asfloat(0u), asfloat(0u));
    r0.w = asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r1.x), asint(r1.y), asint(r1.w))).x);
  }
  o12 = asuint(((g_pixel_selection_active != 0) ? r0.w : asfloat(4294967295u)));
  r4.w = 1.0;
  r1.x = dot(r4.xyzw, v8.xyzw);
  r1.y = dot(r4.xyzw, v9.xyzw);
  r1.z = dot(r4.xyzw, v10.xyzw);
  r1.w = 1.0;
  o0.x = dot(r1.xyzw, motion_rows[0]);
  o0.y = dot(r1.xyzw, motion_rows[1]);
  o0.z = dot(r1.xyzw, motion_rows[2]);
  o0.w = dot(r1.xyzw, motion_rows[3]);
  o3.x = dot(r3.xyz, v8.xyz);
  o3.y = dot(r3.xyz, v9.xyz);
  o3.z = dot(r3.xyz, v10.xyz);
  o6.x = dot(r5.xyz, v8.xyz);
  o6.y = dot(r5.xyz, v9.xyz);
  o6.z = dot(r5.xyz, v10.xyz);
  o4.x = dot(r0.xyz, v8.xyz);
  o4.y = dot(r0.xyz, v9.xyz);
  o4.z = dot(r0.xyz, v10.xyz);
  o1.xyzw = float4(r1.x, r1.y, r1.z, r1.w);
  o2.xyzw = float4(v5.x, v5.y, v5.x, v5.y);
  o3.w = v6.w;
  o4.w = v7.w;
  o5.xy = float2(v5.x, v5.y);
  o5.z = v11.y;
  o5.w = v4.w;
  o6.w = v3.w;
  o7.xy = float2(v11.z, v11.w);
  o7.zw = float2(v12.x, v12.y);
  o8.xy = float2(v12.z, v12.w);
  o8.zw = float2(v15.x, v15.y);
  o9.xy = float2(1.0, 1.0);
  o9.z = v13.y;
  o9.w = v15.z;
  o10.xy = float2(v13.z, v13.w);
  o10.z = v15.w;
  o10.w = v0.y;
  o11.xyzw = float4(v14.x, v14.y, v14.z, v14.w);
  return;
}

void main(
  float4 v0 : POSITION0,
  uint4 v1 : POSITION1,
  float4 v2 : POSITION2,
  float4 v3 : POSITION3,
  float4 v4 : POSITION4,
  float2 v5 : POSITION5,
  float4 v6 : POSITION6,
  float4 v7 : POSITION7,
  float4 v8 : TEXCOORD0,
  float4 v9 : TEXCOORD1,
  float4 v10 : TEXCOORD2,
  float4 v11 : TEXCOORD3,
  float4 v12 : TEXCOORD4,
  float4 v13 : TEXCOORD5,
  float4 v14 : TEXCOORD6,
  float4 v15 : TEXCOORD7,
  uint v16 : SV_InstanceID0,
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
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, v16, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
  const float4 world = o0;
  // Last frame's placement of this mesh nearest this one, with last frame's bones when the pool has them (else the
  // current pose there).
  float4 rows[3] = {v8, v9, v10};
  float4 previous_rows[3];
  uint4 previous_bones;
  float4 previous_world = world;
  if (MotionPreviousPlacement(rows, previous_rows, previous_bones)) {
    motion_previous_entry = MotionPreviousBoneEntry(previous_bones.y);
    motion_previous_base = (int)floor(0.5f + asfloat(previous_bones.z));
    motion_current_base = (int)floor(0.5f + v11.x);
    const float4 previous_base_input = float4((motion_previous_entry != 0u ? asfloat(previous_bones.z) : v11.x), v11.yzw);
    motion_time = motion_times.x;  // last frame's (fur and the like sway with it)
    GameMain(v0, v1, v2, v3, v4, v5, v6, v7, previous_rows[0], previous_rows[1], previous_rows[2], previous_base_input, v12, v13, v14, v15, v16, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, o11, o12);
    previous_world = o0;
    motion_previous_entry = 0u;
  }
  motion_row0 = v8;
  motion_row1 = v9;
  motion_row2 = v10;
  motion_bones = float4(v11.x, g_animation_texture_width.x, 0.f, 0.f);
  motion_position = float4(dot(world, view_projection._m00_m10_m20_m30), dot(world, view_projection._m01_m11_m21_m31),
                           dot(world, view_projection._m02_m12_m22_m32), dot(world, view_projection._m03_m13_m23_m33));
  motion_clip = float4(dot(world, motion_current[0]), dot(world, motion_current[1]), dot(world, motion_current[2]),
                       dot(world, motion_current[3]));
  motion_previous_clip = float4(dot(previous_world, motion_previous[0]), dot(previous_world, motion_previous[1]),
                                dot(previous_world, motion_previous[2]), dot(previous_world, motion_previous[3]));
}
