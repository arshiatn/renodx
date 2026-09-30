// PHARAOH fire01 - flame sprite with depth fade (0x7444BC44). Same family as fire00,
// plus a gbuffer depth read for the soft-particle fade. Also pre-t00.
//
// TWO DECOMPILER BUGS, both confirmed from the bytecode.
//
// 1. w13.xw does not exist. ISGN packs register 13 as NORMAL1 (uint) in .x and
//    OBJECT_POS0 (float3) in .yzw, and dcl_input_ps reads v13.yw - register components
//    y and w, which are OBJECT_POS0.x and OBJECT_POS0.z. 3Dmigoto mapped the first
//    correctly and then copied the register letter for the second, giving .xw on a
//    float3: invalid HLSL, and the wrong component even if it compiled. Correct: .xz.
//
// 2. asm 68   mul r0.zw, r0.zzzw, cb1[0].yyyw
//    Two different fields: cb1[0].y is sp_Alpha_Gamma, cb1[0].w is sp_Alpha_Random_Gamma.
//    3Dmigoto names both after the first, so the random-alpha branch silently used the
//    base alpha gamma and sp_Alpha_Random_Gamma dropped out of the shader entirely.
//
// The other 7 multi-component cbuffer reads are each a single field, so they are fine.

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

cbuffer shader_params : register(b1)
{
  float sp_Alpha_Gain : packoffset(c0);
  float sp_Alpha_Gamma : packoffset(c0.y);
  float sp_Alpha_Lift : packoffset(c0.z);
  float sp_Alpha_Random_Gamma : packoffset(c0.w);
  float sp_Alpha_Random_Offset : packoffset(c1);
  float sp_Burn_Amount_Offset : packoffset(c1.y);
  float3 sp_Burn_Amount_Scale_Ratio : packoffset(c2);
  float sp_Falloff_Horizontal : packoffset(c2.w);
  float sp_Falloff_Vertical : packoffset(c3);
  float sp_Flame_Gain : packoffset(c3.y);
  float sp_Flame_Gamma : packoffset(c3.z);
  float sp_Flame_Lift : packoffset(c3.w);
  float sp_Flame_Mask_Gamma : packoffset(c4);
  float2 sp_Flame_Offset : packoffset(c4.y);
  float2 sp_Flame_Pan_Direction : packoffset(c5);
  float3 sp_Flame_Post_Color_Balance : packoffset(c6);
  float sp_Flame_Post_Gamma : packoffset(c6.w);
  float sp_Flame_Post_Multiplier : packoffset(c7);
  float2 sp_Flame_Tile : packoffset(c7.y);
  float sp_Flames_Base_Green_Multiplier : packoffset(c7.w);
  float sp_Flames_Base_Red_Multiplier : packoffset(c8);
  float sp_Flames_Base_Speed : packoffset(c8.y);
  float sp_LUT_V_Selector : packoffset(c8.z);
  float sp_Noise_Multiplier_X : packoffset(c8.w);
  float sp_Noise_Multiplier_Y : packoffset(c9);
  float sp_Noise_Multiplier_Z : packoffset(c9.y);
  float sp_Noise_Ramp_Gamma : packoffset(c9.z);
  float sp_Noise_Ramp_Multiplier : packoffset(c9.w);
  float sp_Noise_Speed_U : packoffset(c10);
  float sp_Noise_Speed_V : packoffset(c10.y);
  float sp_Noise_Tile : packoffset(c10.z);
  float sp_Random_Offset_Gamma : packoffset(c10.w);
  float sp_depth_fade : packoffset(c11);
  float sp_fade_switch : packoffset(c11.y);
}

SamplerState s_Flame_Mask_s : register(s0);
SamplerState s_Flame_Texture_s : register(s1);
SamplerState s_LUT_s : register(s2);
Texture2DMS<float4> gbuffer_channel_4_texture_ms : register(t0);
Texture2D<float4> gbuffer_channel_4_texture : register(t1);
Texture2D<float4> t_Flame_Mask : register(t2);
Texture2D<float4> t_Flame_Texture : register(t3);
Texture2D<float4> t_LUT : register(t4);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  nointerpolation float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  nointerpolation float4 v8 : TEXCOORD7,
  float3 v9 : TEXCOORD8,
  float4 v10 : COLOR1,
  linear centroid float4 v11 : COLOR2,
  float4 v12 : NORMAL0,
  nointerpolation uint v13 : NORMAL1,
  nointerpolation float3 w13 : OBJECT_POS0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.y = dot(r0.xyz, r0.xyz);
  r0.y = rsqrt(r0.y);
  r0.xy = r0.xz * r0.yy;
  r0.z = (int)v8.z & 255;
  r0.z = (uint)r0.z;
  // FIXED: .xw -> .xz. OBJECT_POS0 sits in register 13 .yzw, and the shader reads
  // register components .y and .w = OBJECT_POS0.x and .z. Sign-preserving frac, i.e. a
  // per-object hash from world position.
  r1.xy = cmp(w13.xz >= -w13.xz);
  r1.zw = frac(abs(w13.xz));
  r1.xy = r1.xy ? r1.zw : -r1.zw;
  r0.w = r1.x + r1.y;
  r0.z = r0.z * 0.00392156886 + r0.w;
  r0.w = cmp(r0.z >= -r0.z);
  r0.z = frac(abs(r0.z));
  r0.z = r0.w ? r0.z : -r0.z;
  r1.w = abs(r0.z);
  r0.z = 1 + r1.w;
  r1.y = r0.z * 0.5 + 0.5;
  r1.xz = float2(1,0);
  r0.z = sp_Flames_Base_Speed * time_in_sec;
  r0.w = sp_Flames_Base_Red_Multiplier * v11.x;
  r2.x = sp_Flames_Base_Green_Multiplier * v11.y;
  r0.w = r0.w * 10 + r2.x;
  r0.z = r0.z * r0.w;
  r0.zw = sp_Flame_Pan_Direction.xy * r0.zz;
  r0.zw = r1.xy * v2.xy + r0.zw;
  r0.zw = r0.zw * sp_Flame_Tile.xy + sp_Flame_Offset.xy;
  r0.zw = r1.zw + r0.zw;
  r0.z = t_Flame_Texture.Sample(s_Flame_Texture_s, r0.zw).x;
  r0.w = t_Flame_Mask.Sample(s_Flame_Mask_s, v2.zw).x;
  r0.w = log2(r0.w);
  r0.w = sp_Flame_Mask_Gamma * r0.w;
  r0.w = exp2(r0.w);
  r0.z = r0.z * r0.w;
  r0.z = log2(r0.z);
  r0.w = sp_Flame_Gamma * r0.z;
  r0.w = exp2(r0.w);
  r1.x = r0.w * sp_Flame_Gain + sp_Flame_Lift;
  r1.y = sp_LUT_V_Selector;
  r1.xyz = t_LUT.Sample(s_LUT_s, r1.xy).xyz;
  r1.xyz = log2(r1.xyz);
  r1.xyz = sp_Flame_Post_Gamma * r1.xyz;
  r1.xyz = exp2(r1.xyz);
  r1.xyz = sp_Flame_Post_Color_Balance.xyz * r1.xyz;
  r1.xyz = sp_Flame_Post_Multiplier * r1.xyz;
  r0.w = log2(r1.w);
  // FIXED: asm 68 is cb1[0].yyyw - two fields, not one.
  r0.z = sp_Alpha_Gamma * r0.z;
  r0.w = sp_Alpha_Random_Gamma * r0.w;
  r0.w = exp2(r0.w);
  r0.w = saturate(sp_Alpha_Random_Offset + r0.w);
  r0.z = exp2(r0.z);
  r0.z = r0.z * sp_Alpha_Gain + sp_Alpha_Lift;
  r0.z = r0.w * r0.z;
  r0.w = floor(v6.w);
  r0.w = 0.00100000005 * r0.w;
  r0.w = r0.w * r0.w;
  r0.z = saturate(r0.z * r0.w);
  r2.y = sp_Falloff_Vertical * inv_view._m21;
  r2.x = inv_view._m20;
  r2.z = inv_view._m22;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xy = r2.xz * r0.ww;
  r0.w = dot(r0.xy, r0.xy);
  r0.w = rsqrt(r0.w);
  r0.xy = r0.xy * r0.ww;
  r0.x = dot(r2.xy, r0.xy);
  r0.x = log2(abs(r0.x));
  r0.x = sp_Falloff_Horizontal * r0.x;
  r0.x = exp2(r0.x);
  r0.x = min(1, r0.x);
  r0.x = r0.z * r0.x;
  r0.yz = -g_viewport_origin.xy + v0.xy;
  r0.yz = g_vpos_texel_offset + r0.yz;
  r0.yz = g_viewport_dimensions.zw * r0.yz;
  r2.xy = g_vpos_texel_offset + v0.xy;
  r2.xy = g_screen_size.zw * r2.xy;
  r0.w = cmp(1 < g_num_of_samples);
  if (r0.w != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r2.zw = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r2.zw = uiDest.xy;
  }
  r3.xy = (uint2)r2.zw;
  r3.xy = r3.xy * r2.xy;
  r3.xy = floor(r3.xy);
  r3.xy = (int2)r3.xy;
  r2.zw = (int2)r2.zw + int2(-1,-1);
  r3.xy = max(int2(0,0), (int2)r3.xy);
  r3.xy = min((int2)r3.xy, (int2)r2.zw);
  if (r0.w != 0) {
    r3.z = 0;
    r4.x = gbuffer_channel_4_texture_ms.Load(r3.xy, 0).x;
  } else {
    r3.w = 0;
    r4.x = gbuffer_channel_4_texture.Load(r3.xyw).x;
  }
  r4.yz = r0.yz * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r3.x = dot(r4.yzxw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r4.yzxw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r4.yzxw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r4.yzxw, inv_view_projection._m03_m13_m23_m33);
  r3.xyz = r3.xyz / r0.yyy;
  r3.xyz = v1.xyz + -r3.xyz;
  r0.y = dot(r3.xyz, r3.xyz);
  r0.y = sqrt(r0.y);
  r0.y = saturate(r0.y / sp_depth_fade);
  r0.y = -1 + r0.y;
  r0.y = saturate(sp_fade_switch * r0.y + 1);
  r0.y = r0.x * r0.y;
  // Same knob as fire00. Alpha (set at the end of the shader) is left alone.
  o0.xyz = r1.xyz * r0.yyy;
  if (HDR >= 0.5f) o0.xyz *= VfxFireBrightness;
  if (r0.w != 0) {
    gbuffer_channel_4_texture_ms.GetDimensions(uiDest.x, uiDest.y, uiDest.z);
    r0.yz = uiDest.xy;
  } else {
    gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
    r0.yz = uiDest.xy;
  }
  r1.xy = (uint2)r0.yz;
  r1.xy = r2.xy * r1.xy;
  r1.xy = floor(r1.xy);
  r1.xy = (int2)r1.xy;
  r0.yz = (int2)r0.yz + int2(-1,-1);
  r1.xy = max(int2(0,0), (int2)r1.xy);
  r1.xy = min((int2)r1.xy, (int2)r0.yz);
  if (r0.w != 0) {
    r1.z = 0;
    r4.x = gbuffer_channel_4_texture_ms.Load(r1.xy, 0).x;
  } else {
    r1.w = 0;
    r4.x = gbuffer_channel_4_texture.Load(r1.xyw).x;
  }
  r1.x = dot(r4.yzxw, inv_view_projection._m00_m10_m20_m30);
  r1.y = dot(r4.yzxw, inv_view_projection._m01_m11_m21_m31);
  r1.z = dot(r4.yzxw, inv_view_projection._m02_m12_m22_m32);
  r0.y = dot(r4.yzxw, inv_view_projection._m03_m13_m23_m33);
  r0.yzw = r1.xyz / r0.yyy;
  r0.yzw = v1.xyz + -r0.yzw;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = sqrt(r0.y);
  r0.y = saturate(r0.y / sp_depth_fade);
  r0.y = -1 + r0.y;
  r0.y = saturate(sp_fade_switch * r0.y + 1);
  o0.w = r0.x * r0.y;
  return;
}