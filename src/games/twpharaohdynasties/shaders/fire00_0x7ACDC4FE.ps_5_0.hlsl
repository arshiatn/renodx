// PHARAOH fire00 - flame sprite (0x7ACDC4FE). Scene particle, drawn into the FP16 scene
// buffer before t00, so the multiplier below feeds the tone mapper and the glow mask.
//
// DECOMPILER BUG: 3Dmigoto dropped "nointerpolation" from the NORMAL1 input. NORMAL1 is a
// uint (ISGN says register 13, mask .x, type uint) and HLSL requires every integer pixel
// shader input to be nointerpolation, so the file will not compile as dumped. 0x7444BC44
// is the same shader family and kept the qualifier, which is how this stands out.
//
// Nothing else is wrong. I scanned every instruction that reads 2+ components of one
// cbuffer row - the usual 3Dmigoto swizzle collapse - and all four (asm 39, 48 x2, 77)
// land inside a single field: sp_Flame_Pan_Direction, sp_Flame_Tile, sp_Flame_Offset,
// sp_Flame_Post_Color_Balance. No collapse here.
//
// Neither NORMAL1 nor OBJECT_POS0 is actually read by this shader (dcl_input_ps lists only
// v2, v3, v6, v8, v11), but both must stay declared, in this order, so the compiler packs
// register 13 the way the vertex shader wrote it.

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
  float sp_Flame_Gain : packoffset(c0.w);
  float sp_Flame_Gamma : packoffset(c1);
  float sp_Flame_Lift : packoffset(c1.y);
  float sp_Flame_Mask_Gamma : packoffset(c1.z);
  float2 sp_Flame_Offset : packoffset(c2);
  float2 sp_Flame_Pan_Direction : packoffset(c2.z);
  float3 sp_Flame_Post_Color_Balance : packoffset(c3);
  float sp_Flame_Post_Gamma : packoffset(c3.w);
  float sp_Flame_Post_Multiplier : packoffset(c4);
  float sp_Flame_Scale_AI_Input : packoffset(c4.y);
  float2 sp_Flame_Tile : packoffset(c4.z);
  float sp_Flames_Base_Speed : packoffset(c5);
  float sp_LUT_V_Selector : packoffset(c5.y);
  float sp_Noise_Multiplier_X : packoffset(c5.z);
  float sp_Noise_Multiplier_Y : packoffset(c5.w);
  float sp_Noise_Multiplier_Z : packoffset(c6);
  float sp_Noise_Ramp_Gamma : packoffset(c6.y);
  float sp_Noise_Ramp_Multiplier : packoffset(c6.z);
  float sp_Noise_Speed_U : packoffset(c6.w);
  float sp_Noise_Speed_V : packoffset(c7);
  float sp_Noise_Tile : packoffset(c7.y);
}

SamplerState s_Flame_Mask_s : register(s0);
SamplerState s_Flame_Texture_s : register(s1);
SamplerState s_LUT_s : register(s2);
Texture2D<float4> t_Flame_Mask : register(t0);
Texture2D<float4> t_Flame_Texture : register(t1);
Texture2D<float4> t_LUT : register(t2);


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
  nointerpolation uint v13 : NORMAL1,      // FIXED: uint PS inputs must be nointerpolation
  nointerpolation float3 w13 : OBJECT_POS0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v3.xyz, v3.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r0.y = dot(r0.xyz, r0.xyz);
  r0.y = rsqrt(r0.y);
  r0.xy = r0.xz * r0.yy;
  r0.z = dot(r0.xy, r0.xy);
  r0.z = rsqrt(r0.z);
  r0.xy = r0.xy * r0.zz;
  r1.y = 0.150000006 * inv_view._m21;
  r1.x = inv_view._m20;
  r1.z = inv_view._m22;
  r0.z = dot(r1.xyz, r1.xyz);
  r0.z = rsqrt(r0.z);
  r0.zw = r1.xz * r0.zz;
  r0.x = dot(r0.zw, r0.xy);
  r0.x = log2(abs(r0.x));
  r0.x = 1.5 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = min(1, r0.x);
  r0.y = sp_Flames_Base_Speed * time_in_sec;
  r0.z = v11.x * 10 + v11.y;
  r0.y = r0.y * r0.z;
  r0.yz = sp_Flame_Pan_Direction.xy * r0.yy;
  r1.x = 1;
  r0.w = (int)v8.z & 255;
  r0.w = (uint)r0.w;
  r2.xy = float2(0.00392156886,0.784313798) * r0.ww;
  r1.zw = sin(r2.xy);
  r0.w = 1 + r1.z;
  r1.y = r0.w * 0.5 + 0.5;
  r0.yz = r1.xy * v2.xy + r0.yz;
  r0.yz = r0.yz * sp_Flame_Tile.xy + sp_Flame_Offset.xy;
  r2.z = 0;
  r0.yz = r2.zx + r0.yz;
  r0.y = t_Flame_Texture.Sample(s_Flame_Texture_s, r0.yz).x;
  r0.z = t_Flame_Mask.Sample(s_Flame_Mask_s, v2.zw).x;
  r0.z = log2(r0.z);
  r0.z = sp_Flame_Mask_Gamma * r0.z;
  r0.z = exp2(r0.z);
  r0.y = r0.y * r0.z;
  r0.y = log2(r0.y);
  r0.z = sp_Alpha_Gamma * r0.y;
  r0.y = sp_Flame_Gamma * r0.y;
  r0.y = exp2(r0.y);
  r1.x = r0.y * sp_Flame_Gain + sp_Flame_Lift;
  r0.y = exp2(r0.z);
  r0.y = r0.y * sp_Alpha_Gain + sp_Alpha_Lift;
  r0.z = abs(r1.w) * abs(r1.w);
  r0.z = abs(r1.w) * r0.z + 0.150000006;
  r0.y = r0.z * r0.y;
  r0.z = floor(v6.w);
  r0.z = 0.00100000005 * r0.z;
  r0.z = r0.z * r0.z;
  r0.y = saturate(r0.y * r0.z);
  r0.x = r0.y * r0.x;
  r1.y = sp_LUT_V_Selector;
  r0.yzw = t_LUT.Sample(s_LUT_s, r1.xy).xyz;
  r0.yzw = log2(r0.yzw);
  r0.yzw = sp_Flame_Post_Gamma * r0.yzw;
  r0.yzw = exp2(r0.yzw);
  r0.yzw = sp_Flame_Post_Color_Balance.xyz * r0.yzw;
  r0.yzw = sp_Flame_Post_Multiplier * r0.yzw;
  // LUT colour x flame intensity. Scene-referred, so the multiplier goes through the
  // tone mapper rather than straight to the display. Alpha is untouched - it drives the
  // blend, and scaling it would change coverage, not brightness.
  o0.xyz = r0.yzw * r0.xxx;
  if (HDR >= 0.5f) o0.xyz *= VfxFireBrightness;
  o0.w = r0.x;
  return;
}