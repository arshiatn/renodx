// fx_pointlight, linear falloff, point shadow map: the game's, x the light's traced visibility (twwarhammer3/addon.cpp traced_lights;
// twattila/rt_local_lights.hlsli). Runs only with the mask bound; otherwise the game's own bytes do.
#include "../../twattila/rt_local_lights.hlsli"

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

cbuffer dynamic_light_PS : register(b1)
{
  float4 g_viewport_offset_scale : packoffset(c0);
}

cbuffer pointlight_shadowmap_cb : register(b2)
{
  float4 g_shadow_buffer_size : packoffset(c0);
  float g_shadow_sample_bias : packoffset(c1);
  float g_shadow_soft_edges : packoffset(c1.y);
  float2 g_light_proj_zw : packoffset(c1.z);
  float g_light_width : packoffset(c2);
  float g_point_light_shadow_map_sample_bias : packoffset(c2.y);
}

SamplerState s_point_light_shadow_map_s : register(s1);
SamplerComparisonState s_point_light_shadow_map_sm50_s : register(s0);
Texture2D<float4> gbuffer_channel_1_texture : register(t0);
Texture2D<float4> gbuffer_channel_2_texture : register(t1);
Texture2D<float4> gbuffer_channel_3_texture : register(t2);
Texture2D<float4> gbuffer_channel_4_texture : register(t3);
Texture2DArray<float> t_point_light_shadow_map : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : POSITION1,
  float4 v3 : POSITION2,
  out float4 o0 : SV_Target0)
{
  const float4 icb[] = { { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 1.000000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0.800000, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 1.000000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 1.000000, 0.200000, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0.200000, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0},
                              { 0, 0, 0, 0} };
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cmp(0 != g_shadow_soft_edges);
  r0.yz = g_screen_size.zw * v0.xy;
  gbuffer_channel_4_texture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.xy = uiDest.xy;
  r1.zw = (uint2)r1.xy;
  r0.yz = r1.zw * r0.yz;
  r0.yz = floor(r0.yz);
  r0.yz = (int2)r0.yz;
  r1.xy = (int2)r1.xy + int2(-1,-1);
  r0.yz = max(int2(0,0), (int2)r0.yz);
  r1.xy = min((int2)r0.yz, (int2)r1.xy);
  r1.zw = float2(0,0);
  r0.yz = gbuffer_channel_1_texture.Load(r1.xyw).xy;
  r2.xyz = gbuffer_channel_2_texture.Load(r1.xyw).xyz;
  r0.w = gbuffer_channel_3_texture.Load(r1.xyw).z;
  r1.z = gbuffer_channel_4_texture.Load(r1.xyz).x;
  r0.yz = float2(1023,1023) * r0.yz;
  r0.yz = round(r0.yz);
  r0.yz = (uint2)r0.yz;
  r0.yz = (uint2)r0.yz >> int2(2,2);
  r0.yz = (uint2)r0.yz;
  r3.xy = r0.yz * float2(0.00784313772,0.00784313772) + float2(-1,-1);
  r3.zw = float2(1,-1);
  r4.z = dot(r3.xyz, -r3.xyw);
  r0.y = max(0, r4.z);
  r0.y = sqrt(r0.y);
  r4.xy = r3.xy * r0.yy;
  r3.xyz = r4.xyz * float3(2,2,-2) + float3(0,0,1);
  r4.x = dot(r3.xyz, inv_view._m00_m10_m20);
  r4.y = dot(r3.xyz, inv_view._m01_m11_m21);
  r4.z = dot(r3.xyz, inv_view._m02_m12_m22);
  r0.y = saturate(1 + -r0.w);
  r0.zw = v0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r0.zw = r0.zw / g_viewport_offset_scale.zw;
  r1.xy = r0.zw * float2(2,-2) + float2(-1,1);
  r1.w = 1;
  r3.x = dot(r1.xyzw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r1.xyzw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r1.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.z = dot(r1.xyzw, inv_view_projection._m03_m13_m23_m33);
  r1.xyz = r3.xyz / r0.zzz;
  r3.xyz = -v3.xyz + r1.xyz;
  r5.xyz = g_point_light_shadow_map_sample_bias * r3.xyz;
  r0.z = max(abs(r5.x), abs(r5.y));
  r0.z = max(r0.z, abs(r5.z));
  r0.z = g_light_proj_zw.y / r0.z;
  r0.z = g_light_proj_zw.x + r0.z;
  r0.w = dot(r5.xyz, r5.xyz);
  r0.w = rsqrt(r0.w);
  r5.xyz = r5.xyz * r0.www;
  r6.xzw = -r5.zxy;
  r7.xyzw = cmp(abs(r5.yzxz) < abs(r5.xxyy));
  r8.xyz = cmp(float3(0,0,0) < r5.xyz);
  r6.y = 0;
  r9.xw = float2(1,2);
  r9.yz = float2(1,-1) * r5.zy;
  r10.xzw = r8.xxx ? r6.yxw : r9.xyz;
  r7.xy = r7.yw ? r7.xz : 0;
  r8.x = 3;
  r8.w = r6.x;
  r9.xw = r8.yy ? r9.wy : r8.xw;
  r7.zw = cmp(abs(r5.yx) < abs(r5.zz));
  r0.w = r7.w ? r7.z : 0;
  r11.x = 4;
  r11.zw = float2(1,-1) * r5.xy;
  r6.x = 5;
  r6.xzw = r8.zzz ? r11.xzw : r6.xzw;
  r6.y = abs(r5.z);
  r6.xyzw = r0.wwww ? r6.xyzw : float4(0,1,0,0);
  r9.y = abs(r5.y);
  r9.z = r11.z;
  r6.xyzw = r7.yyyy ? r9.xyzw : r6.xyzw;
  r10.y = abs(r5.x);
  r5.xyzw = r7.xxxx ? r10.xyzw : r6.xyzw;
  r5.yz = r5.zw / r5.yy;
  r5.yz = float2(1,1) + r5.yz;
  r6.xy = float2(0.5,0.5) * r5.yz;
  r5.w = (int)r5.x;
  r0.w = cmp((int)r0.x == -1);
  if (r0.w != 0) {
    r7.xy = g_shadow_buffer_size.xy * r6.xy + float2(0.5,0.5);
    r7.zw = floor(r7.xy);
    r7.xy = r7.xy + -r7.zw;
    r8.xy = -r7.xy * g_shadow_buffer_size.zw + r6.xy;
    r9.y = -3 * g_shadow_buffer_size.z;
    r7.zw = float2(0,0);
    r0.w = -3;
    while (true) {
      r1.w = cmp(3 < (int)r0.w);
      if (r1.w != 0) break;
      r1.w = (int)r0.w;
      r9.z = g_shadow_buffer_size.w * r1.w;
      r5.yz = r9.yz + r8.xy;
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.yzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.zzzz);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r1.w = dot(r11.xyzw, float4(1,1,1,1));
      r2.w = dot(r10.xyzw, r11.xyzw);
      r5.x = g_shadow_buffer_size.z * 2 + r5.y;
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r5.xzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.zzzz);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r3.w = dot(r11.xyzw, float4(1,1,1,1));
      r1.w = r3.w + r1.w;
      r3.w = dot(r10.xyzw, r11.xyzw);
      r2.w = r3.w + r2.w;
      r10.y = g_shadow_buffer_size.z * 2 + r5.x;
      r10.zw = r5.zw;
      r11.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.yzw).xyzw;
      r12.xyzw = cmp(r11.xyzw >= r0.zzzz);
      r12.xyzw = r12.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r3.w = dot(r12.xyzw, float4(1,1,1,1));
      r1.w = r3.w + r1.w;
      r3.w = dot(r11.xyzw, r12.xyzw);
      r2.w = r3.w + r2.w;
      r10.x = g_shadow_buffer_size.z * 2 + r10.y;
      r10.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r10.xzw).xyzw;
      r11.xyzw = cmp(r10.xyzw >= r0.zzzz);
      r11.xyzw = r11.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r3.w = dot(r11.xyzw, float4(1,1,1,1));
      r1.w = r3.w + r1.w;
      r3.w = dot(r10.xyzw, r11.xyzw);
      r2.w = r3.w + r2.w;
      r7.w = r7.w + r1.w;
      r7.z = r7.z + r2.w;
      r0.w = (int)r0.w + 2;
    }
    r0.w = cmp(r7.w < 64);
    if (r0.w != 0) {
      r0.w = cmp(0 < r7.w);
      r1.w = r7.z / r7.w;
      r1.w = r0.z / r1.w;
      r1.w = -1 + r1.w;
      r5.y = saturate(g_light_width * r1.w);
      r5.x = 0;
      r5.xy = r0.ww ? r5.xy : float2(1,0);
      if (r0.w != 0) {
        r0.w = r5.y * r5.y;
        r1.w = r0.w * r5.y;
        r2.w = 3 * r0.w;
        r3.w = 1 + -r5.y;
        r4.w = r3.w * r2.w;
        r5.z = r3.w * r3.w;
        r6.w = r5.z * r5.y;
        r7.z = 3 * r6.w;
        r7.w = r5.z * r3.w;
        r8.w = 0;
        r9.x = 0;
        while (true) {
          r9.y = cmp((int)r8.w >= 11);
          if (r9.y != 0) break;
          r9.y = r0.w * r5.y + r9.x;
          r9.z = (int)r8.w * 11;
          r9.w = r4.w * icb[r9.z+1].x + r1.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+2].x * r4.w;
          r9.w = r7.z * icb[r9.z+2].y + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+3].x * r4.w;
          r9.w = r7.z * icb[r9.z+3].y + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+4].y * r7.z;
          r9.w = r7.w * icb[r9.z+4].z + r9.w;
          r9.w = r4.w * icb[r9.z+4].x + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+5].y * r7.z;
          r9.w = r7.w * icb[r9.z+5].z + r9.w;
          r9.w = r4.w * icb[r9.z+5].x + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+6].y * r7.z;
          r9.w = r7.w * icb[r9.z+6].z + r9.w;
          r9.w = r4.w * icb[r9.z+6].x + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+7].x * r4.w;
          r9.w = r7.z * icb[r9.z+7].y + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.w = icb[r9.z+8].x * r4.w;
          r9.w = r7.z * icb[r9.z+8].y + r9.w;
          r9.w = r0.w * r5.y + r9.w;
          r9.y = r9.y + r9.w;
          r9.z = r4.w * icb[r9.z+9].x + r1.w;
          r9.y = r9.y + r9.z;
          r9.x = r0.w * r5.y + r9.y;
          r8.w = (int)r8.w + 1;
        }
        r8.z = r5.w;
        r10.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,-5)).xyzw;
        r8.w = 1 + -r7.y;
        r9.y = -r1.w * r7.x + r1.w;
        r9.zw = r10.wx * r9.yy;
        r10.xz = r10.zy * r1.ww + r9.zw;
        r9.z = r10.z * r7.y;
        r9.z = r8.w * r10.x + r9.z;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,-5)).xyzw;
        r10.x = dot(r11.wz, r1.ww);
        r9.z = r8.w * r10.x + r9.z;
        r10.x = dot(r11.xy, r1.ww);
        r9.z = r7.y * r10.x + r9.z;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,-5)).xyzw;
        r10.x = dot(r12.wz, r1.ww);
        r9.z = r8.w * r10.x + r9.z;
        r10.x = dot(r12.xy, r1.ww);
        r9.z = r7.y * r10.x + r9.z;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,-5)).xyzw;
        r10.x = dot(r13.wz, r1.ww);
        r9.z = r8.w * r10.x + r9.z;
        r10.x = dot(r13.xy, r1.ww);
        r9.z = r7.y * r10.x + r9.z;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,-5)).xyzw;
        r10.x = dot(r14.wz, r1.ww);
        r9.z = r8.w * r10.x + r9.z;
        r10.x = dot(r14.xy, r1.ww);
        r9.z = r7.y * r10.x + r9.z;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,-5)).xyzw;
        r10.xz = r15.zy * r7.xx;
        r10.xz = r10.xz * r1.ww;
        r10.xw = r15.wx * r1.ww + r10.xz;
        r9.z = r8.w * r10.x + r9.z;
        r9.z = r7.y * r10.w + r9.z;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,-3)).xyzw;
        r10.x = 0.200000003 * r4.w;
        r10.w = r4.w * 0.200000003 + r1.w;
        r11.z = r0.w * r5.y + -r10.w;
        r11.z = r7.x * r11.z + r10.w;
        r12.zw = r16.yz * r11.zz;
        r12.zw = r16.xw * r9.yy + r12.zw;
        r9.z = r8.w * r12.w + r9.z;
        r9.z = r7.y * r12.z + r9.z;
        r9.w = r10.y * r11.z + r9.w;
        r9.z = r8.w * r9.w + r9.z;
        r9.z = r7.y * r12.w + r9.z;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,-3)).xyzw;
        r13.zw = r6.ww * float2(3,0.600000024) + r4.ww;
        r13.zw = r0.ww * r5.yy + r13.zw;
        r4.w = -r13.w + r10.w;
        r4.w = r7.x * r4.w + r13.w;
        r14.zw = r16.wx * r4.ww;
        r15.yz = r16.zy * r13.ww + r14.zw;
        r6.w = r8.w * r15.y + r9.z;
        r6.w = r7.y * r15.z + r6.w;
        r9.z = dot(r11.xy, r10.ww);
        r6.w = r8.w * r9.z + r6.w;
        r9.z = dot(r16.wz, r10.ww);
        r6.w = r7.y * r9.z + r6.w;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,-3)).xyzw;
        r9.z = dot(r17.wz, r13.ww);
        r6.w = r8.w * r9.z + r6.w;
        r9.z = dot(r17.xy, r13.ww);
        r6.w = r7.y * r9.z + r6.w;
        r9.z = dot(r12.xy, r10.ww);
        r6.w = r8.w * r9.z + r6.w;
        r9.z = dot(r17.wz, r10.ww);
        r6.w = r7.y * r9.z + r6.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,-3)).xyzw;
        r9.z = dot(r18.wz, r13.ww);
        r6.w = r8.w * r9.z + r6.w;
        r9.z = dot(r18.xy, r13.ww);
        r6.w = r7.y * r9.z + r6.w;
        r9.z = dot(r13.xy, r10.ww);
        r6.w = r8.w * r9.z + r6.w;
        r9.z = dot(r18.wz, r10.ww);
        r6.w = r7.y * r9.z + r6.w;
        r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,-3)).xyzw;
        r9.z = r13.w + -r10.w;
        r9.z = r7.x * r9.z + r10.w;
        r11.xy = r19.zy * r9.zz;
        r11.xw = r19.wx * r13.ww + r11.xy;
        r6.w = r8.w * r11.x + r6.w;
        r6.w = r7.y * r11.w + r6.w;
        r9.w = dot(r14.xy, r10.ww);
        r6.w = r8.w * r9.w + r6.w;
        r9.w = dot(r19.wz, r10.ww);
        r6.w = r7.y * r9.w + r6.w;
        r20.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,-3)).xyzw;
        r9.w = r7.x * r10.x + r1.w;
        r10.xy = r20.yz * r7.xx;
        r10.xy = r10.xy * r1.ww;
        r10.xy = r20.xw * r9.ww + r10.xy;
        r6.w = r8.w * r10.y + r6.w;
        r6.w = r7.y * r10.x + r6.w;
        r10.z = r15.x * r9.w + r10.z;
        r6.w = r8.w * r10.z + r6.w;
        r6.w = r7.y * r10.y + r6.w;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,-1)).xyzw;
        r10.yz = r15.yz * r11.zz;
        r10.yz = r15.xw * r9.yy + r10.yz;
        r6.w = r8.w * r10.z + r6.w;
        r6.w = r7.y * r10.y + r6.w;
        r6.w = r8.w * r12.z + r6.w;
        r6.w = r7.y * r10.z + r6.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,-1)).xyzw;
        r11.xw = r13.wz + -r13.zw;
        r11.xw = r7.xx * r11.xw + r13.zw;
        r12.yz = r12.yz * r11.xx;
        r12.xy = r12.xw * r4.ww + r12.yz;
        r6.w = r8.w * r12.y + r6.w;
        r6.w = r7.y * r12.x + r6.w;
        r10.z = r16.y * r11.x + r14.w;
        r6.w = r8.w * r10.z + r6.w;
        r6.w = r7.y * r12.y + r6.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,-1)).xyzw;
        r7.w = r7.w * 0.800000012 + r7.z;
        r7.w = r2.w * r3.w + r7.w;
        r7.w = r0.w * r5.y + r7.w;
        r10.z = r13.z + -r7.w;
        r10.z = r7.x * r10.z + r7.w;
        r12.yz = r14.wx * r10.zz;
        r12.yw = r14.zy * r7.ww + r12.yz;
        r6.w = r8.w * r12.y + r6.w;
        r6.w = r7.y * r12.w + r6.w;
        r12.y = dot(r17.xy, r13.zz);
        r6.w = r8.w * r12.y + r6.w;
        r12.y = dot(r14.wz, r13.zz);
        r6.w = r7.y * r12.y + r6.w;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,-1)).xyzw;
        r12.y = r7.w + -r13.z;
        r12.y = r7.x * r12.y + r13.z;
        r13.xy = r15.zy * r12.yy;
        r14.xz = r15.wx * r7.ww + r13.xy;
        r6.w = r8.w * r14.x + r6.w;
        r6.w = r7.y * r14.z + r6.w;
        r12.w = dot(r18.xy, r13.zz);
        r6.w = r8.w * r12.w + r6.w;
        r12.w = dot(r15.wz, r13.zz);
        r6.w = r7.y * r12.w + r6.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,-1)).xyzw;
        r14.xz = r16.yz * r9.zz;
        r14.xz = r16.xw * r11.ww + r14.xz;
        r6.w = r8.w * r14.z + r6.w;
        r6.w = r7.y * r14.x + r6.w;
        r11.y = r19.x * r11.w + r11.y;
        r6.w = r8.w * r11.y + r6.w;
        r6.w = r7.y * r14.z + r6.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,-1)).xyzw;
        r14.zw = r16.yz * r7.xx;
        r14.zw = r14.zw * r1.ww;
        r14.zw = r16.xw * r9.ww + r14.zw;
        r6.w = r8.w * r14.w + r6.w;
        r6.w = r7.y * r14.z + r6.w;
        r6.w = r8.w * r10.x + r6.w;
        r6.w = r7.y * r14.w + r6.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,1)).xyzw;
        r15.yz = r16.yz * r11.zz;
        r15.yz = r16.xw * r9.yy + r15.yz;
        r6.w = r8.w * r15.z + r6.w;
        r6.w = r7.y * r15.y + r6.w;
        r6.w = r8.w * r10.y + r6.w;
        r6.w = r7.y * r15.z + r6.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,1)).xyzw;
        r10.xy = r16.yz * r11.xx;
        r10.xy = r16.xw * r4.ww + r10.xy;
        r6.w = r8.w * r10.y + r6.w;
        r6.w = r7.y * r10.x + r6.w;
        r6.w = r8.w * r12.x + r6.w;
        r6.w = r7.y * r10.y + r6.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,1)).xyzw;
        r10.yz = r16.wx * r10.zz;
        r12.xw = r16.zy * r7.ww + r10.yz;
        r6.w = r8.w * r12.x + r6.w;
        r6.w = r7.y * r12.w + r6.w;
        r5.z = r5.z * r3.w + r7.z;
        r2.w = r2.w * r3.w + r5.z;
        r0.w = r0.w * r5.y + r2.w;
        r2.w = r7.w + -r0.w;
        r2.w = r7.x * r2.w + r0.w;
        r3.w = r14.y * r2.w + r12.z;
        r3.w = r8.w * r3.w + r6.w;
        r2.w = r16.z * r2.w + r10.y;
        r2.w = r7.y * r2.w + r3.w;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,1)).xyzw;
        r5.yz = r17.zy * r12.yy;
        r10.yz = r17.wx * r7.ww + r5.yz;
        r2.w = r8.w * r10.y + r2.w;
        r2.w = r7.y * r10.z + r2.w;
        r0.w = r0.w + -r7.w;
        r0.w = r7.x * r0.w + r7.w;
        r3.w = r15.x * r0.w + r13.y;
        r2.w = r8.w * r3.w + r2.w;
        r0.w = r17.w * r0.w + r5.y;
        r0.w = r7.y * r0.w + r2.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,1)).xyzw;
        r5.yz = r12.yz * r9.zz;
        r5.yz = r12.xw * r11.ww + r5.yz;
        r0.w = r8.w * r5.z + r0.w;
        r0.w = r7.y * r5.y + r0.w;
        r0.w = r8.w * r14.x + r0.w;
        r0.w = r7.y * r5.z + r0.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,1)).xyzw;
        r7.zw = r12.yz * r7.xx;
        r7.zw = r7.zw * r1.ww;
        r7.zw = r12.xw * r9.ww + r7.zw;
        r0.w = r8.w * r7.w + r0.w;
        r0.w = r7.y * r7.z + r0.w;
        r0.w = r8.w * r14.z + r0.w;
        r0.w = r7.y * r7.w + r0.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,3)).xyzw;
        r10.yz = r12.yz * r11.zz;
        r10.yz = r12.xw * r9.yy + r10.yz;
        r0.w = r8.w * r10.z + r0.w;
        r0.w = r7.y * r10.y + r0.w;
        r0.w = r8.w * r15.y + r0.w;
        r0.w = r7.y * r10.z + r0.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,3)).xyzw;
        r13.xy = r12.wx * r4.ww;
        r14.xy = r12.zy * r13.ww + r13.xy;
        r0.w = r8.w * r14.x + r0.w;
        r0.w = r7.y * r14.y + r0.w;
        r0.w = r8.w * r10.x + r0.w;
        r2.w = r12.z * r11.x + r13.x;
        r0.w = r7.y * r2.w + r0.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,3)).xyzw;
        r2.w = dot(r14.wz, r13.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r14.xy, r13.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r16.xy, r13.zz);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r14.wz, r13.zz);
        r0.w = r7.y * r2.w + r0.w;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,3)).xyzw;
        r2.w = dot(r15.wz, r13.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r15.xy, r13.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r17.xy, r13.zz);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r15.wz, r13.zz);
        r0.w = r7.y * r2.w + r0.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,3)).xyzw;
        r10.xz = r16.zy * r9.zz;
        r11.xy = r16.wx * r13.ww + r10.xz;
        r0.w = r8.w * r11.x + r0.w;
        r0.w = r7.y * r11.y + r0.w;
        r0.w = r8.w * r5.y + r0.w;
        r2.w = r16.w * r11.w + r10.x;
        r0.w = r7.y * r2.w + r0.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,3)).xyzw;
        r5.yz = r13.yz * r7.xx;
        r5.yz = r5.yz * r1.ww;
        r5.yz = r13.xw * r9.ww + r5.yz;
        r0.w = r8.w * r5.z + r0.w;
        r0.w = r7.y * r5.y + r0.w;
        r0.w = r8.w * r7.z + r0.w;
        r0.w = r7.y * r5.z + r0.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-5,5)).xyzw;
        r7.zw = r13.wx * r9.yy;
        r9.yz = r13.zy * r1.ww + r7.zw;
        r0.w = r8.w * r9.y + r0.w;
        r0.w = r7.y * r9.z + r0.w;
        r0.w = r8.w * r10.y + r0.w;
        r2.w = r13.z * r11.z + r7.z;
        r0.w = r7.y * r2.w + r0.w;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-3,5)).xyzw;
        r2.w = dot(r11.wz, r1.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.xy, r1.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r12.xy, r10.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.wz, r10.ww);
        r0.w = r7.y * r2.w + r0.w;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(-1,5)).xyzw;
        r2.w = dot(r11.wz, r1.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.xy, r1.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r14.xy, r10.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.wz, r10.ww);
        r0.w = r7.y * r2.w + r0.w;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(1,5)).xyzw;
        r2.w = dot(r11.wz, r1.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.xy, r1.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r15.xy, r10.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.wz, r10.ww);
        r0.w = r7.y * r2.w + r0.w;
        r11.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(3,5)).xyzw;
        r2.w = dot(r11.wz, r1.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.xy, r1.ww);
        r0.w = r7.y * r2.w + r0.w;
        r2.w = dot(r16.xy, r10.ww);
        r0.w = r8.w * r2.w + r0.w;
        r2.w = dot(r11.wz, r10.ww);
        r0.w = r7.y * r2.w + r0.w;
        r10.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r8.xyz, r0.z, int2(5,5)).xyzw;
        r7.xz = r10.zy * r7.xx;
        r7.xz = r7.xz * r1.ww;
        r7.zw = r10.wx * r1.ww + r7.xz;
        r0.w = r8.w * r7.z + r0.w;
        r0.w = r7.y * r7.w + r0.w;
        r0.w = r8.w * r5.y + r0.w;
        r1.w = r10.w * r9.w + r7.x;
        r0.w = r7.y * r1.w + r0.w;
        r5.x = r0.w / r9.x;
      }
    } else {
      r5.x = 0;
    }
  } else {
    r5.x = 1;
  }
  if (r0.x == 0) {
    r6.z = r5.w;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r6.xyz).x;
    r0.x = cmp(r0.z < r0.x);
    r5.x = r0.x ? 1.000000 : 0;
  }
  r0.xzw = -camera_position.xyz + r1.xyz;
  r1.x = dot(r0.xzw, r0.xzw);
  r1.x = rsqrt(r1.x);
  r1.yzw = r1.xxx * r0.xzw;
  r2.w = dot(r3.xyz, r3.xyz);
  r3.w = rsqrt(r2.w);
  r3.xyz = r3.xyz * r3.www;
  r2.w = sqrt(r2.w);
  r2.w = saturate(r2.w / v3.w);
  r2.w = 1 + -r2.w;
  r3.w = dot(r1.yzw, r4.xyz);
  r4.w = r3.w + r3.w;
  r5.yzw = r4.xyz * -r4.www + r1.yzw;
  r4.w = dot(-r3.xyz, r5.yzw);
  r4.w = max(-1, r4.w);
  r4.w = min(1, r4.w);
  r5.y = 1 + -abs(r4.w);
  r5.y = sqrt(r5.y);
  r5.z = abs(r4.w) * -0.0187292993 + 0.0742610022;
  r5.z = r5.z * abs(r4.w) + -0.212114394;
  r5.z = r5.z * abs(r4.w) + 1.57072878;
  r5.w = r5.z * r5.y;
  r5.w = r5.w * -2 + 3.14159274;
  r4.w = cmp(r4.w < -r4.w);
  r4.w = r4.w ? r5.w : 0;
  r4.w = r5.z * r5.y + r4.w;
  r5.y = r0.y * -1.12524891 + 1.12839997;
  r6.xyz = float3(-0.00872664619,0.00872664619,0.00872664619) + r4.www;
  r4.w = 1 / r5.y;
  r6.xyz = r6.xyz * r4.www;
  r5.zw = float2(0.707106769,0.707106769) * r6.xy;
  r6.y = abs(r5.z) * abs(r5.z);
  r6.w = r6.y * abs(r5.z);
  r7.x = r6.w * abs(r5.z);
  r5.z = abs(r5.z) * 0.278393 + 1;
  r5.z = r6.y * 0.230388999 + r5.z;
  r5.z = r6.w * 0.000972000009 + r5.z;
  r5.z = r7.x * 0.0781079978 + r5.z;
  r5.z = r5.z * r5.z;
  r5.z = r5.z * r5.z;
  r5.z = 1 / r5.z;
  r5.z = 1 + -r5.z;
  r6.y = cmp(0 < r6.x);
  r6.x = cmp(r6.x < 0);
  r6.x = (int)-r6.y + (int)r6.x;
  r6.x = (int)r6.x;
  r5.z = r6.x * r5.z + 1;
  r5.z = 0.5 * r5.z;
  r6.x = r5.w * r5.w;
  r6.y = r6.x * r5.w;
  r5.w = r6.y * r5.w;
  r6.z = r6.z * 0.196853578 + 1;
  r6.x = r6.x * 0.230388999 + r6.z;
  r6.x = r6.y * 0.000972000009 + r6.x;
  r5.w = r5.w * 0.0781079978 + r6.x;
  r5.w = r5.w * r5.w;
  r5.w = r5.w * r5.w;
  r5.w = 1 / r5.w;
  r5.w = 2 + -r5.w;
  r5.z = r5.w * 0.5 + -r5.z;
  r6.xy = float2(-1.11072075,1.11072075) * r4.ww;
  r6.zw = r6.xy * r6.xy;
  r5.w = r6.z * -r6.x;
  r6.x = r5.w * -r6.x;
  r4.w = r4.w * 0.309216887 + 1;
  r7.xy = r6.zw * float2(0.230388999,0.230388999) + r4.ww;
  r4.w = r5.w * 0.000972000009 + r7.x;
  r4.w = r6.x * 0.0781079978 + r4.w;
  r4.w = r4.w * r4.w;
  r4.w = r4.w * r4.w;
  r4.w = 1 / r4.w;
  r4.w = 1 + -r4.w;
  r4.w = 1 + -r4.w;
  r4.w = 0.5 * r4.w;
  r5.w = r6.w * r6.y;
  r6.x = r5.w * r6.y;
  r5.w = r5.w * 0.000972000009 + r7.y;
  r5.w = r6.x * 0.0781079978 + r5.w;
  r5.w = r5.w * r5.w;
  r5.w = r5.w * r5.w;
  r5.w = 1 / r5.w;
  r5.w = 2 + -r5.w;
  r4.w = r5.w * 0.5 + -r4.w;
  r4.w = abs(r5.z) / r4.w;
  r4.w = min(1, r4.w);
  r4.w = r4.w * r4.w;
  r4.w = 0.785398185 * r4.w;
  r0.y = 1 + -r0.y;
  r5.z = dot(r4.xyz, -r3.xyz);
  r5.w = saturate(r5.z);
  r6.x = 1.57079637 * r0.y;
  r6.x = sin(r6.x);
  r6.y = -1 + r5.w;
  r6.x = r6.x * r6.y + 1;
  r4.x = saturate(dot(r4.xyz, -r1.yzw));
  r0.y = r0.y * r0.y;
  r4.y = 0.5 * r0.y;
  r0.y = -r0.y * 0.5 + 1;
  r4.z = r5.w * r0.y + r4.y;
  r4.z = 1 / r4.z;
  r0.y = r4.x * r0.y + r4.y;
  r0.y = 1 / r0.y;
  r0.y = r4.z * r0.y;
  r0.y = 0.25 * r0.y;
  r0.y = min(1, r0.y);
  r0.y = r0.y * r6.x;
  r0.xzw = r0.xzw * r1.xxx + -r3.xyz;
  r1.x = dot(r0.xzw, r0.xzw);
  r1.x = rsqrt(r1.x);
  r0.xzw = r1.xxx * r0.xzw;
  r0.x = saturate(dot(r1.yzw, r0.xzw));
  r0.z = r0.x * -5.55472994 + -6.98316002;
  r0.x = r0.z * r0.x;
  r0.x = exp2(r0.x);
  r0.x = r0.x * 0.959999979 + 0.0399999991;
  r0.y = r4.w * r0.y;
  r0.x = r0.y * r0.x;
  r0.y = r5.w * -5.55472994 + -6.98316002;
  r0.y = r0.y * r5.w;
  r0.y = exp2(r0.y);
  r0.y = r0.y * 0.959999979 + 0.0399999991;
  r0.z = r5.y * r5.y;
  r1.xy = r5.yy * r5.yy + float2(0.330000013,0.0900000036);
  r0.w = r1.x + r1.x;
  r0.w = r0.z / r0.w;
  r0.yw = float2(1,1) + -r0.yw;
  r0.z = 0.449999988 * r0.z;
  r0.z = r0.z / r1.y;
  r1.x = max(-1, r5.z);
  r1.y = max(-1, r3.w);
  r1.xy = min(float2(1,1), r1.xy);
  r1.z = 1 + -abs(r1.x);
  r1.z = sqrt(r1.z);
  r1.w = abs(r1.x) * -0.0187292993 + 0.0742610022;
  r1.w = r1.w * abs(r1.x) + -0.212114394;
  r1.w = r1.w * abs(r1.x) + 1.57072878;
  r3.x = r1.w * r1.z;
  r3.x = r3.x * -2 + 3.14159274;
  r3.y = cmp(r1.x < -r1.x);
  r3.x = r3.y ? r3.x : 0;
  r1.z = r1.w * r1.z + r3.x;
  r1.w = 1 + -abs(r1.y);
  r1.w = sqrt(r1.w);
  r3.x = abs(r1.y) * -0.0187292993 + 0.0742610022;
  r3.x = r3.x * abs(r1.y) + -0.212114394;
  r3.x = r3.x * abs(r1.y) + 1.57072878;
  r3.y = r3.x * r1.w;
  r3.y = r3.y * -2 + 3.14159274;
  r1.y = cmp(r1.y < -r1.y);
  r1.y = r1.y ? r3.y : 0;
  r1.y = r3.x * r1.w + r1.y;
  r1.w = max(r1.y, r1.z);
  r3.x = min(r1.y, r1.z);
  r1.y = r1.z + -r1.y;
  r1.y = cos(r1.y);
  r1.y = max(0, r1.y);
  r0.z = r1.y * r0.z;
  r1.y = sin(r1.w);
  r0.z = r1.y * r0.z;
  sincos(r3.x, r3.x, r4.x);
  r1.y = r3.x / r4.x;
  r0.z = r0.z * r1.y + r0.w;
  r0.w = max(0, r1.x);
  r0.z = r0.z * r0.w;
  r1.xyz = v2.xyz * r0.yyy;
  r1.xyz = r1.xyz * r2.xyz;
  r0.yzw = r1.xyz * r0.zzz;
  r0.yzw = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r0.yzw;
  r0.xyz = r0.xxx * v2.xyz + r0.yzw;
  r0.xyz = r5.xxx * r0.xyz;
  o0.xyz = r2.www * r0.xyz;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  o0.w = 1;
  return;
}