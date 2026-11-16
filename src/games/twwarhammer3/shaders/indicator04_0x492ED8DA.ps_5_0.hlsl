// 0x492ED8DA (fx_indicator): fx_indicator_mboit's resolve: 709BE774's colour weighted by the moments' transmittance.
// Unit/Building Indicator Brightness (SI.someindicators) scales the colour. The add-on draws this copy only with the
// slider off 1 in an HDR mode; otherwise the game's own bytes run (addon.cpp, BuildRuntimeData). At 1 this copy is within
// 2.2e-4 of the game's on random inputs (WARP: 2,782 of 131,072 values apart, fxc orders the moment solve differently);
// [earlydepthstencil] is the game's forceEarlyDepthStencil.

cbuffer camera : register(b0)
{
  float3 camera_position : packoffset(c0);
  float3 prev_camera_position : packoffset(c1);
  float4x4 view : packoffset(c2);
  float4x4 projection : packoffset(c6);
  float4x4 view_projection : packoffset(c10);
  float4x4 prev_view_projection : packoffset(c14);
  float4x4 inv_view : packoffset(c18);
  float4x4 prev_inv_view : packoffset(c22);
  float4x4 inv_projection : packoffset(c26);
  float4x4 inv_view_projection : packoffset(c30);
  float4 camera_near_far : packoffset(c34);
  float time_in_sec : packoffset(c35);
  float prev_time_in_sec : packoffset(c35.y);
  float real_time_in_sec : packoffset(c35.z);
  float update_time_in_sec : packoffset(c35.w);
  float2 g_inverse_focal_length : packoffset(c36);
  float g_vertical_fov : packoffset(c36.z);
  float g_aspect_ratio : packoffset(c36.w);
  float4 g_screen_size : packoffset(c37);
  float g_vpos_texel_offset : packoffset(c38);
  float4 g_viewport_dimensions : packoffset(c39);
  float2 g_viewport_origin : packoffset(c40);
  float4 g_render_target_dimensions : packoffset(c41);
  float4 g_camera_temp0 : packoffset(c42);
  float4 g_camera_temp1 : packoffset(c43);
  float4 g_camera_temp2 : packoffset(c44);
  float4 g_clip_rect : packoffset(c45);
  float3 g_vr_head_rotation : packoffset(c46);
  int g_num_of_samples : packoffset(c46.w);
  float g_supersampling : packoffset(c47);
  float4 g_mouse_position : packoffset(c48);
  float3 g_frustum_points[8] : packoffset(c49);
  float4 g_frustum_planes[6] : packoffset(c57);
  float g_orthographic : packoffset(c63);
  float g_overlay_lerp : packoffset(c63.y);
  float g_overlay_parchment_lerp : packoffset(c63.z);
  float g_overlay_show_details : packoffset(c63.w);
  float g_amount_shadow_in_far_distance : packoffset(c64);
  float2 g_camera_jitter : packoffset(c64.y);
  float2 g_prev_camera_jitter : packoffset(c65);
  uint g_debug_visualization_flags : packoffset(c65.z);
  bool g_taa_is_enabled : packoffset(c65.w);
}

cbuffer MBOIT_CB : register(b1)
{
  float4 wrapping_zone_parameters : packoffset(c0);
  float overestimation : packoffset(c1);
  float moment_bias : packoffset(c1.y);
  float moments_buffer_scaler : packoffset(c1.z);
  float blend_op_kill_threshold : packoffset(c1.w);
}

SamplerState indicator_texture_sampler_s : register(s0);
Texture2D<float4> gbuffer_channel_4_texture : register(t0);
Texture2D<float4> g_indicator_texture : register(t1);
Texture2D<float4> moments : register(t2);
Texture2D<float4> extra_moments : register(t3);
Texture2D<float4> zeroth_moment : register(t4);


// 3Dmigoto declarations
#define cmp -
#include "../shared.h"


[earlydepthstencil]
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  float3 v5 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = moments_buffer_scaler * v0.xy;
  r0.xy = (int2)r0.xy;
  r0.zw = float2(0,0);
  r1.x = zeroth_moment.Load(r0.xyw).x;
  r1.y = -0.00100050028 + r1.x;
  r1.y = cmp(r1.y < 0);
  if (r1.y != 0) discard;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.yz = uiDest.xy;
  r2.xy = (uint2)r1.yz;
  r1.yz = (int2)r1.yz + int2(-1,-1);
  r2.zw = g_screen_size.zw * v0.xy;
  r2.xy = r2.zw * r2.xy;
  r3.xy = r2.zw * float2(2,-2) + float2(-1,1);
  r2.xy = floor(r2.xy);
  r2.xy = (int2)r2.xy;
  r2.xy = max(int2(0,0), (int2)r2.xy);
  r2.xy = min((int2)r2.xy, (int2)r1.yz);
  r2.zw = float2(0,0);
  r3.z = gbuffer_channel_4_texture.Load(r2.xyz).x;
  r3.w = 1;
  r1.y = dot(r3.xyzw, inv_projection._m02_m12_m22_m32);
  r1.z = dot(r3.xyzw, inv_projection._m03_m13_m23_m33);
  r1.y = r1.y / r1.z;
  r1.z = v4.z / v4.w;
  r1.w = cmp(r1.y < r1.z);
  r1.z = r1.z + -r1.y;
  r1.y = saturate(20 + -r1.y);
  r2.xy = cmp(r1.zz < float2(0.5,5));
  r1.z = 0.0500000007 + r1.z;
  r1.z = saturate(3.33333325 * r1.z);
  r1.y = r1.y * -r1.z + 1;
  r1.z = r2.y ? 1 : 0.0700000003;
  r1.z = r2.x ? r1.z : 0;
  r1.z = r1.w ? r1.z : 1;
  r1.y = r1.z * r1.y;
  r2.xyzw = g_indicator_texture.Sample(indicator_texture_sampler_s, v1.xy).xyzw;
  r2.xyzw = v3.xyzw * r2.xyzw;
  r1.z = r2.w * r1.y + 0.00100000005;
  r1.y = r2.w * r1.y;
  r2.xyz = r2.xyz + r2.xyz;
  r2.xyz = r2.xyz * r1.yyy;
  r1.y = max(0.00100000005, r1.z);
  r1.y = min(0.999000013, r1.y);
  r1.z = 1 + -r1.y;
  r1.z = log2(r1.z);
  r1.z = -r1.z * 0.693147182 + -blend_op_kill_threshold;
  r1.z = cmp(r1.z < 0);
  if (r1.z != 0) discard;
  r3.xy = moments.Load(r0.xyw).xy;
  r0.xyzw = extra_moments.Load(r0.xyz).xyzw;
  r4.x = r3.y;
  r4.yz = r0.yw;
  r3.yz = r0.xz;
  r0.xyz = r3.xyz / r1.xxx;
  r0.xyw = moment_bias * -r0.zxy + r0.zxy;
  r3.xyz = r4.xyz / r1.xxx;
  r4.xyz = float3(0.479999989,0.451000005,0.449999988) + -r3.xyz;
  r3.xyz = moment_bias * r4.xyz + r3.xyz;
  r1.z = -r3.x * r3.x + r3.y;
  r1.w = -r0.y * r3.x + r0.w;
  r4.xyz = -r0.yyw * r0.yww + r3.xyz;
  r2.w = 1 / r4.x;
  r5.x = r2.w * r1.w;
  r1.z = -r1.w * r5.x + r1.z;
  r3.z = 1 / r1.z;
  r1.z = -r3.x * r0.w + r0.x;
  r5.y = r4.y * r2.w;
  r4.w = -r1.w * r5.y + r1.z;
  r5.z = r4.w * r3.z;
  r1.z = dot(r4.yw, r5.yz);
  r1.z = r4.z + -r1.z;
  r3.w = 1 / r1.z;
  r4.y = v5.z + -r0.y;
  r1.z = r5.x * r4.y + r3.x;
  r0.z = r3.x;
  r4.z = v5.z * v5.z + -r1.z;
  r1.z = dot(r5.yz, r4.yz);
  r1.z = r1.z + r0.w;
  r1.w = v5.z * v5.z;
  r4.w = r1.w * v5.z + -r1.z;
  r3.yw = r4.zw * r3.zw;
  r3.z = -r5.z * r3.w + r3.y;
  r1.z = dot(r5.xy, r3.zw);
  r3.y = r4.y * r2.w + -r1.z;
  r1.z = dot(r0.yzw, r3.yzw);
  r3.x = 1 + -r1.z;
  r3.xyz = r3.xyz / r3.www;
  r3.w = 0.333333343 * r3.y;
  r4.xy = float2(0.333333343,0.333333343) * r3.yz;
  r4.z = -r4.x;
  r5.x = dot(r4.yz, r3.xw);
  r1.z = -r4.y * r4.y;
  r1.z = r3.y * 0.333333343 + r1.z;
  r6.x = 4 * r1.z;
  r5.y = -r4.x * r4.y + r3.x;
  r1.w = r1.z * r3.z;
  r1.z = sqrt(-r1.z);
  r1.z = r1.z + r1.z;
  r1.w = r1.w * -0.666666687 + r5.y;
  r6.y = -r5.y;
  r2.w = dot(r6.xy, r5.xy);
  r2.w = sqrt(r2.w);
  r3.x = max(r2.w, abs(r1.w));
  r3.x = 1 / r3.x;
  r3.y = min(r2.w, abs(r1.w));
  r3.x = r3.y * r3.x;
  r3.y = r3.x * r3.x;
  r3.z = r3.y * 0.0208350997 + -0.0851330012;
  r3.z = r3.y * r3.z + 0.180141002;
  r3.z = r3.y * r3.z + -0.330299497;
  r3.y = r3.y * r3.z + 0.999866009;
  r3.z = r3.x * r3.y;
  r3.z = r3.z * -2 + 1.57079637;
  r3.w = cmp(abs(r1.w) < r2.w);
  r3.z = r3.w ? r3.z : 0;
  r3.x = r3.x * r3.y + r3.z;
  r3.y = cmp(-r1.w < r1.w);
  r3.y = r3.y ? -3.14159274 : 0;  // the game's l(0xc0490fdb): -pi to the bit (the decompile printed -3.141593, one ulp off)
  r3.x = r3.x + r3.y;
  r3.y = min(r2.w, -r1.w);
  r1.w = max(r2.w, -r1.w);
  r1.w = cmp(r1.w >= -r1.w);
  r2.w = cmp(r3.y < -r3.y);
  r1.w = r1.w ? r2.w : 0;
  r1.w = r1.w ? -r3.x : r3.x;
  r1.w = 0.333333343 * r1.w;
  sincos(r1.w, r3.x, r5.x);
  r5.y = r3.x;
  r5.z = dot(float2(-0.5,-0.866025388), r5.xy);
  r5.w = dot(float2(-0.5,0.866025388), r5.xy);
  r3.xyz = r1.zzz * r5.xzw + -r4.yyy;
  r4.xyzw = cmp(v5.zzzz < r3.xyzx);
  r1.z = r4.y ? -0 : -1;
  r4.xyzw = r4.xyzw ? float4(0,0,0,-0) : float4(1,1,1,-1);
  r1.z = r4.z + r1.z;
  r5.xyz = r3.yzz + -r3.xyx;
  r1.w = r4.y + r4.w;
  r2.w = -overestimation + r4.x;
  r1.zw = r1.zw / r5.yx;
  r1.z = r1.z + -r1.w;
  r1.z = r1.z / r5.z;
  r4.xyz = -v5.zzz + r3.xyz;
  r2.w = r2.w / r4.x;
  r1.w = -r2.w + r1.w;
  r1.w = r1.w / r4.y;
  r1.z = r1.z + -r1.w;
  r4.w = r1.z / r4.z;
  r1.z = -r4.w * r3.y + r1.w;
  r1.w = r4.w * -r3.x + r1.z;
  r1.z = r1.z * -r3.x + r2.w;
  r4.y = r1.w * -v5.z + r1.z;
  r4.z = r4.w * -v5.z + r1.w;
  r4.x = r1.z * -v5.z + overestimation;
  r0.x = 1;
  r0.x = dot(r4.xyzw, r0.xyzw);
  r0.x = -r1.x * r0.x;
  r0.x = 1.44269502 * r0.x;
  r0.x = exp2(r0.x);
  r0.x = min(1, r0.x);
  o0.xyz = r0.xxx * r2.xyz;
  o0.w = r0.x * r1.y;
  o0.xyz *= SI.someindicators;
  return;
}
