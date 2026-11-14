// Attila world motion (anti_aliasing.hpp): the game's 0x3FC5424C with its world position for ray tracing's stream output
// (rt_scene.hpp).
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
}

cbuffer rigid_config_PS : register(b1)
{
  float g_unlit : packoffset(c0);
  float g_dissolve_enable : packoffset(c0.y);
  float g_untextured : packoffset(c0.z);
  float g_snow : packoffset(c0.w);
  float g_rain : packoffset(c1);
  float g_tesselation_factor : packoffset(c1.y);
  float g_tesselation_displacement_scale : packoffset(c1.z);
  float2 g_uv2_tile_interval : packoffset(c2);
  float2 g_texel_density_uv_scale : packoffset(c2.z);
  float4 g_decal_uv_rect_coords : packoffset(c3);
  float g_force_snow_shader : packoffset(c4);
  float g_is_second_class_object : packoffset(c4.y);
  int g_pixel_selection_active : packoffset(c4.z);
  int g_pixel_selection_start_pixel_index : packoffset(c4.w);
}

cbuffer variant_animation_size : register(b2)
{
  float2 g_animation_texture_height : packoffset(c0);
  float2 g_animation_texture_width : packoffset(c0.z);
}

Texture2D<float4> t_animation_map : register(t0);
Texture2D<uint4> t_pixel_selection_object_id_texture : register(t1);

#include "./motion.hlsli"
#include "../twattila/rt_capture.hlsli"

static float motion_time;
static float4 motion_rows[4];

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
  float2 v14,
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
  inout uint4 p10,
  inout float4 o11)
{
  float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17;
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
  p10 = 0;
  o11 = 0;
  r0.xyzw = float4((v1.z * 255.00999450683594), (v1.y * 255.00999450683594), (v1.x * 255.00999450683594), (v1.w * 255.00999450683594));
  r0.xyzw = float4(floor(r0.x), floor(r0.y), floor(r0.z), floor(r0.w));
  r0.xyzw = float4(asfloat((int)r0.x), asfloat((int)r0.y), asfloat((int)r0.z), asfloat((int)r0.w));
  r1.x = (v2.y + v2.z);
  r1.x = (r1.x + v2.x);
  r1.yz = float2((v2.y + -0.0039059999398887157), (v2.x + -0.0039059999398887157));
  r1.xw = float2((-(r1.x) + 1.0), (-(r1.x) + 0.9960939884185791));
  r1.yzw = float3(saturate((r1.y * 3.4028234663852886e+38)), saturate((r1.z * 3.4028234663852886e+38)), saturate((r1.w * 3.4028234663852886e+38)));
  r2.xy = float2((r1.y * v2.y), (r1.z * v2.x));
  r1.x = (r1.w * r1.x);
  r1.y = asfloat(((0.0 != r1.y) ? 0xffffffffu : 0u));
  r0.yzw = float3(((asint(r1.y) != 0) ? r0.y : r0.x), ((asint(r1.y) != 0) ? r0.z : r0.x), ((asint(r1.y) != 0) ? r0.w : r0.x));
  r1.y = (g_animation_texture_width.x + 0.5);
  r1.y = floor(r1.y);
  r1.y = asfloat((int)r1.y);
  r1.z = (v10.x + 0.5);
  r1.z = floor(r1.z);
  r0.x = asfloat((asint(r0.x) * 3));
  r0.x = (float)asint(r0.x);
  r0.x = (r0.x + r1.z);
  r0.x = asfloat((int)r0.x);
  r1.w = asfloat((asuint(r1.y) ^ asuint(r0.x)));
  r2.z = asfloat(max(asint(r0.x), -(asint(r0.x))));
  r2.w = asfloat(max(asint(r1.y), -(asint(r1.y))));
  {
    const uint together0_0 = (asuint(r2.z) / asuint(r2.w));
    r2.z = asfloat(together0_0);
  }
  r3.x = asfloat((-asint(r2.z)));
  r1.w = asfloat((asuint(r1.w) & 2147483648u));
  r3.y = ((asint(r1.w) != 0) ? r3.x : r2.z);
  r3.x = asfloat((-(asint(r3.y)) * asint(r1.y) + asint(r0.x)));
  r4.xy = float2(asfloat((asint(r3.x) + 1)), asfloat((asint(r3.x) + 2)));
  r3.zw = float2(asfloat(0u), asfloat(0u));
  r5.xyzw = float4(t_animation_map.Load(int3(asint(r3.x), asint(r3.y), asint(r3.w))).x, t_animation_map.Load(int3(asint(r3.x), asint(r3.y), asint(r3.w))).y, t_animation_map.Load(int3(asint(r3.x), asint(r3.y), asint(r3.w))).z, t_animation_map.Load(int3(asint(r3.x), asint(r3.y), asint(r3.w))).w);
  r4.zw = float2(r3.y, r3.w);
  r3.xyzw = float4(t_animation_map.Load(int3(asint(r4.x), asint(r4.z), asint(r4.w))).x, t_animation_map.Load(int3(asint(r4.x), asint(r4.z), asint(r4.w))).y, t_animation_map.Load(int3(asint(r4.x), asint(r4.z), asint(r4.w))).z, t_animation_map.Load(int3(asint(r4.x), asint(r4.z), asint(r4.w))).w);
  r4.xyzw = float4(t_animation_map.Load(int3(asint(r4.y), asint(r4.z), asint(r4.w))).x, t_animation_map.Load(int3(asint(r4.y), asint(r4.z), asint(r4.w))).y, t_animation_map.Load(int3(asint(r4.y), asint(r4.z), asint(r4.w))).z, t_animation_map.Load(int3(asint(r4.y), asint(r4.z), asint(r4.w))).w);
  r0.xyz = float3(asfloat((asint(r0.y) * 3)), asfloat((asint(r0.z) * 3)), asfloat((asint(r0.w) * 3)));
  r0.xyz = float3((float)asint(r0.x), (float)asint(r0.y), (float)asint(r0.z));
  r0.xyz = float3((r0.x + r1.z), (r0.y + r1.z), (r0.z + r1.z));
  r0.xyz = float3(asfloat((int)r0.x), asfloat((int)r0.y), asfloat((int)r0.z));
  r6.xyz = float3(asfloat((asuint(r1.y) ^ asuint(r0.x))), asfloat((asuint(r1.y) ^ asuint(r0.y))), asfloat((asuint(r1.y) ^ asuint(r0.z))));
  r7.xyz = float3(asfloat(max(asint(r0.x), -(asint(r0.x)))), asfloat(max(asint(r0.y), -(asint(r0.y)))), asfloat(max(asint(r0.z), -(asint(r0.z)))));
  {
    const uint together0_0 = (asuint(r7.x) / asuint(r2.w));
    const uint together0_1 = (asuint(r7.y) / asuint(r2.w));
    const uint together0_2 = (asuint(r7.z) / asuint(r2.w));
    r7.xyz = float3(asfloat(together0_0), asfloat(together0_1), asfloat(together0_2));
  }
  r8.xyz = float3(asfloat((-asint(r7.x))), asfloat((-asint(r7.y))), asfloat((-asint(r7.z))));
  r6.xyz = float3(asfloat((asuint(r6.x) & 2147483648u)), asfloat((asuint(r6.y) & 2147483648u)), asfloat((asuint(r6.z) & 2147483648u)));
  r6.xyz = float3(((asint(r6.x) != 0) ? r8.x : r7.x), ((asint(r6.z) != 0) ? r8.z : r7.z), ((asint(r6.y) != 0) ? r8.y : r7.y));
  r0.xyz = float3(asfloat((-(asint(r6.x)) * asint(r1.y) + asint(r0.x))), asfloat((-(asint(r6.z)) * asint(r1.y) + asint(r0.y))), asfloat((-(asint(r6.y)) * asint(r1.y) + asint(r0.z))));
  r7.xyzw = float4(asfloat((asint(r0.y) + 2)), asfloat((asint(r0.x) + 1)), asfloat((asint(r0.x) + 2)), asfloat((asint(r0.y) + 1)));
  r8.x = r0.x;
  r8.y = r6.x;
  r8.z = asfloat(0u);
  r9.xyzw = float4(t_animation_map.Load(int3(asint(r8.x), asint(r8.y), asint(r8.z))).x, t_animation_map.Load(int3(asint(r8.x), asint(r8.y), asint(r8.z))).y, t_animation_map.Load(int3(asint(r8.x), asint(r8.y), asint(r8.z))).z, t_animation_map.Load(int3(asint(r8.x), asint(r8.y), asint(r8.z))).w);
  r8.w = r7.y;
  r10.xyzw = float4(t_animation_map.Load(int3(asint(r8.w), asint(r8.y), asint(r8.z))).x, t_animation_map.Load(int3(asint(r8.w), asint(r8.y), asint(r8.z))).y, t_animation_map.Load(int3(asint(r8.w), asint(r8.y), asint(r8.z))).z, t_animation_map.Load(int3(asint(r8.w), asint(r8.y), asint(r8.z))).w);
  r11.x = r7.z;
  r11.yzw = float3(r8.y, r8.z, r8.z);
  r8.xyzw = float4(t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.w))).x, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.w))).y, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.w))).z, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.w))).w);
  r11.x = r0.y;
  r11.y = r6.z;
  r11.z = asfloat(0u);
  r12.xyzw = float4(t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.z))).x, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.z))).y, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.z))).z, t_animation_map.Load(int3(asint(r11.x), asint(r11.y), asint(r11.z))).w);
  r11.w = r7.w;
  r13.xyzw = float4(t_animation_map.Load(int3(asint(r11.w), asint(r11.y), asint(r11.z))).x, t_animation_map.Load(int3(asint(r11.w), asint(r11.y), asint(r11.z))).y, t_animation_map.Load(int3(asint(r11.w), asint(r11.y), asint(r11.z))).z, t_animation_map.Load(int3(asint(r11.w), asint(r11.y), asint(r11.z))).w);
  r7.yzw = float3(r11.y, r11.z, r11.z);
  r7.xyzw = float4(t_animation_map.Load(int3(asint(r7.x), asint(r7.y), asint(r7.w))).x, t_animation_map.Load(int3(asint(r7.x), asint(r7.y), asint(r7.w))).y, t_animation_map.Load(int3(asint(r7.x), asint(r7.y), asint(r7.w))).z, t_animation_map.Load(int3(asint(r7.x), asint(r7.y), asint(r7.w))).w);
  r11.xy = float2(asfloat((asint(r0.z) + 1)), asfloat((asint(r0.z) + 2)));
  r6.x = r0.z;
  r6.zw = float2(asfloat(0u), asfloat(0u));
  r0.xyzw = float4(t_animation_map.Load(int3(asint(r6.x), asint(r6.y), asint(r6.w))).x, t_animation_map.Load(int3(asint(r6.x), asint(r6.y), asint(r6.w))).y, t_animation_map.Load(int3(asint(r6.x), asint(r6.y), asint(r6.w))).z, t_animation_map.Load(int3(asint(r6.x), asint(r6.y), asint(r6.w))).w);
  r11.zw = float2(r6.y, r6.w);
  r6.xyzw = float4(t_animation_map.Load(int3(asint(r11.x), asint(r11.z), asint(r11.w))).x, t_animation_map.Load(int3(asint(r11.x), asint(r11.z), asint(r11.w))).y, t_animation_map.Load(int3(asint(r11.x), asint(r11.z), asint(r11.w))).z, t_animation_map.Load(int3(asint(r11.x), asint(r11.z), asint(r11.w))).w);
  r11.xyzw = float4(t_animation_map.Load(int3(asint(r11.y), asint(r11.z), asint(r11.w))).x, t_animation_map.Load(int3(asint(r11.y), asint(r11.z), asint(r11.w))).y, t_animation_map.Load(int3(asint(r11.y), asint(r11.z), asint(r11.w))).z, t_animation_map.Load(int3(asint(r11.y), asint(r11.z), asint(r11.w))).w);
  r14.xyz = float3(v0.x, v0.y, v0.z);
  r14.w = 1.0;
  r15.x = dot(r14.xyzw, r5.xyzw);
  r15.y = dot(r14.xyzw, r3.xyzw);
  r15.z = dot(r14.xyzw, r4.xyzw);
  r16.x = dot(r14.xyzw, r9.xyzw);
  r16.y = dot(r14.xyzw, r10.xyzw);
  r16.z = dot(r14.xyzw, r8.xyzw);
  r1.yzw = float3((r2.x * r16.x), (r2.x * r16.y), (r2.x * r16.z));
  r1.yzw = float3((v2.z * r15.x + r1.y), (v2.z * r15.y + r1.z), (v2.z * r15.z + r1.w));
  r15.x = dot(r14.xyzw, r12.xyzw);
  r15.y = dot(r14.xyzw, r13.xyzw);
  r15.z = dot(r14.xyzw, r7.xyzw);
  r1.yzw = float3((r2.y * r15.x + r1.y), (r2.y * r15.y + r1.z), (r2.y * r15.z + r1.w));
  r15.x = dot(r14.xyzw, r0.xyzw);
  r15.y = dot(r14.xyzw, r6.xyzw);
  r15.z = dot(r14.xyzw, r11.xyzw);
  r14.xyz = float3((r1.x * r15.x + r1.y), (r1.x * r15.y + r1.z), (r1.x * r15.z + r1.w));
  r1.yzw = float3((v5.z * 2.0 + -1.0), (v5.y * 2.0 + -1.0), (v5.x * 2.0 + -1.0));
  r0.w = dot(r1.yzw, r1.yzw);
  r0.w = rsqrt(r0.w);
  r1.yzw = float3((r0.w * r1.y), (r0.w * r1.z), (r0.w * r1.w));
  r15.x = dot(r1.yzw, r5.xyz);
  r15.y = dot(r1.yzw, r3.xyz);
  r15.z = dot(r1.yzw, r4.xyz);
  r16.x = dot(r1.yzw, r9.xyz);
  r16.y = dot(r1.yzw, r10.xyz);
  r16.z = dot(r1.yzw, r8.xyz);
  r16.xyz = float3((r2.x * r16.x), (r2.x * r16.y), (r2.x * r16.z));
  r15.xyz = float3((v2.z * r15.x + r16.x), (v2.z * r15.y + r16.y), (v2.z * r15.z + r16.z));
  r16.x = dot(r1.yzw, r12.xyz);
  r16.y = dot(r1.yzw, r13.xyz);
  r16.z = dot(r1.yzw, r7.xyz);
  r15.xyz = float3((r2.y * r16.x + r15.x), (r2.y * r16.y + r15.y), (r2.y * r16.z + r15.z));
  r16.x = dot(r1.yzw, r0.xyz);
  r16.y = dot(r1.yzw, r6.xyz);
  r16.z = dot(r1.yzw, r11.xyz);
  r1.yzw = float3((r1.x * r16.x + r15.x), (r1.x * r16.y + r15.y), (r1.x * r16.z + r15.z));
  r15.xyz = float3((v3.z * 2.0 + -1.0), (v3.y * 2.0 + -1.0), (v3.x * 2.0 + -1.0));
  r0.w = dot(r15.xyz, r15.xyz);
  r0.w = rsqrt(r0.w);
  r15.xyz = float3((r0.w * r15.x), (r0.w * r15.y), (r0.w * r15.z));
  r16.x = dot(r15.xyz, r5.xyz);
  r16.y = dot(r15.xyz, r3.xyz);
  r16.z = dot(r15.xyz, r4.xyz);
  r17.x = dot(r15.xyz, r9.xyz);
  r17.y = dot(r15.xyz, r10.xyz);
  r17.z = dot(r15.xyz, r8.xyz);
  r17.xyz = float3((r2.x * r17.x), (r2.x * r17.y), (r2.x * r17.z));
  r16.xyz = float3((v2.z * r16.x + r17.x), (v2.z * r16.y + r17.y), (v2.z * r16.z + r17.z));
  r17.x = dot(r15.xyz, r12.xyz);
  r17.y = dot(r15.xyz, r13.xyz);
  r17.z = dot(r15.xyz, r7.xyz);
  r16.xyz = float3((r2.y * r17.x + r16.x), (r2.y * r17.y + r16.y), (r2.y * r17.z + r16.z));
  r17.x = dot(r15.xyz, r0.xyz);
  r17.y = dot(r15.xyz, r6.xyz);
  r17.z = dot(r15.xyz, r11.xyz);
  r15.xyz = float3((r1.x * r17.x + r16.x), (r1.x * r17.y + r16.y), (r1.x * r17.z + r16.z));
  r16.xyz = float3((v6.z * 2.0 + -1.0), (v6.y * 2.0 + -1.0), (v6.x * 2.0 + -1.0));
  r0.w = dot(r16.xyz, r16.xyz);
  r0.w = rsqrt(r0.w);
  r16.xyz = float3((r0.w * r16.x), (r0.w * r16.y), (r0.w * r16.z));
  r5.x = dot(r16.xyz, r5.xyz);
  r5.y = dot(r16.xyz, r3.xyz);
  r5.z = dot(r16.xyz, r4.xyz);
  r3.x = dot(r16.xyz, r9.xyz);
  r3.y = dot(r16.xyz, r10.xyz);
  r3.z = dot(r16.xyz, r8.xyz);
  r2.xzw = float3((r2.x * r3.x), (r2.x * r3.y), (r2.x * r3.z));
  r2.xzw = float3((v2.z * r5.x + r2.x), (v2.z * r5.y + r2.z), (v2.z * r5.z + r2.w));
  r3.x = dot(r16.xyz, r12.xyz);
  r3.y = dot(r16.xyz, r13.xyz);
  r3.z = dot(r16.xyz, r7.xyz);
  r2.xyz = float3((r2.y * r3.x + r2.x), (r2.y * r3.y + r2.z), (r2.y * r3.z + r2.w));
  r0.x = dot(r16.xyz, r0.xyz);
  r0.y = dot(r16.xyz, r6.xyz);
  r0.z = dot(r16.xyz, r11.xyz);
  r0.xyz = float3((r1.x * r0.x + r2.x), (r1.x * r0.y + r2.y), (r1.x * r0.z + r2.z));
  if (g_pixel_selection_active != 0) {
    r2.x = asfloat((asint(v15) + g_pixel_selection_start_pixel_index));
    r2.yzw = float3(asfloat(0u), asfloat(0u), asfloat(0u));
    r2.xyzw = float4(asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r2.x), asint(r2.y), asint(r2.w))).x), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r2.x), asint(r2.y), asint(r2.w))).y), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r2.x), asint(r2.y), asint(r2.w))).z), asfloat(t_pixel_selection_object_id_texture.Load(int3(asint(r2.x), asint(r2.y), asint(r2.w))).w));
  }
  p10 = asuint(((g_pixel_selection_active != 0) ? r2.x : asfloat(4294967295u)));
  r14.w = 1.0;
  r2.x = dot(r14.xyzw, v7.xyzw);
  r2.y = dot(r14.xyzw, v8.xyzw);
  r2.z = dot(r14.xyzw, v9.xyzw);
  r2.w = 1.0;
  o0.x = dot(r2.xyzw, motion_rows[0]);
  o0.y = dot(r2.xyzw, motion_rows[1]);
  o0.z = dot(r2.xyzw, motion_rows[2]);
  o0.w = dot(r2.xyzw, motion_rows[3]);
  o3.x = dot(r15.xyz, v7.xyz);
  o3.y = dot(r15.xyz, v8.xyz);
  o3.z = dot(r15.xyz, v9.xyz);
  o6.x = dot(r1.yzw, v7.xyz);
  o6.y = dot(r1.yzw, v8.xyz);
  o6.z = dot(r1.yzw, v9.xyz);
  o4.x = dot(r0.xyz, v7.xyz);
  o4.y = dot(r0.xyz, v8.xyz);
  o4.z = dot(r0.xyz, v9.xyz);
  o1.xyzw = float4(r2.x, r2.y, r2.z, r2.w);
  o2.xyzw = float4(v4.x, v4.y, v4.x, v4.y);
  o3.w = v4.x;
  o4.w = v4.y;
  o6.w = 1.0;
  o7.xy = float2(v10.z, v10.w);
  o7.zw = float2(v11.x, v11.y);
  o8.xy = float2(v11.z, v11.w);
  o8.zw = float2(v14.x, v14.y);
  o11.xyzw = float4(v13.x, v13.y, v13.z, v13.w);
  o5.xy = float2(v4.x, v4.y);
  o5.z = v10.y;
  o9.xy = float2(1.0, 1.0);
  o9.z = v12.y;
  o10.xy = float2(v12.z, v12.w);
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
  float2 v14 : TEXCOORD7,
  uint v15 : SV_InstanceID0,
  out float4 motion_position : SV_Position0,
  out float4 motion_world : TEXCOORD0)
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
  uint4 p10 = 0;
  float4 o11 = 0;
  // Identity rows: the world position comes out as the clip position.
  motion_rows[0] = float4(1, 0, 0, 0);
  motion_rows[1] = float4(0, 1, 0, 0);
  motion_rows[2] = float4(0, 0, 1, 0);
  motion_rows[3] = float4(0, 0, 0, 1);
  motion_time = time_in_sec;
  GameMain(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15, o0, o1, o2, o3, o4, o5, o6, o7, o8, o9, o10, p10, o11);
  const float4 world = o0;
#if defined(RT_CAPTURE_UV)
  motion_world = RtWorldUv(world.xyz, o2.xy);
#else
  motion_world = world;
#endif
  motion_position = 0.f;
}
