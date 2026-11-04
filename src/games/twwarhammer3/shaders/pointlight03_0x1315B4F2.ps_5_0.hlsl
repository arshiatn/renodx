// fx_pointlight, linear falloff, point shadow map, aux G-buffer: the game's, x the light's traced visibility (twwarhammer3/addon.cpp traced_lights;
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
Texture2D<float4> gbuffer_channel_aux_texture : register(t4);
Texture2DArray<float> t_point_light_shadow_map : register(t5);


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
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15,r16,r17,r18,r19,r20,r21,r22;
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
  r3.xyz = gbuffer_channel_3_texture.Load(r1.xyw).xyw;
  r4.z = gbuffer_channel_4_texture.Load(r1.xyw).x;
  r1.xyzw = gbuffer_channel_aux_texture.Load(r1.xyz).xyzw;
  r5.xy = r0.yz * float2(2,2) + float2(-1,-1);
  r5.zw = float2(1,-1);
  r6.z = dot(r5.xyz, -r5.xyw);
  r0.y = max(0, r6.z);
  r0.y = sqrt(r0.y);
  r6.xy = r5.xy * r0.yy;
  r0.yzw = r6.xyz * float3(2,2,-2) + float3(0,0,1);
  r5.x = dot(r0.yzw, inv_view._m00_m10_m20);
  r5.y = dot(r0.yzw, inv_view._m01_m11_m21);
  r5.z = dot(r0.yzw, inv_view._m02_m12_m22);
  r0.y = 255 * r3.z;
  r0.y = round(r0.y);
  r0.y = (uint)r0.y;
  r6.xyz = float3(-0.0399999991,-0.0399999991,-0.0399999991) + r2.xyz;
  r6.xyz = r3.xxx * r6.xyz + float3(0.0399999991,0.0399999991,0.0399999991);
  r0.zw = float2(1,1) + -r3.yx;
  r2.xyz = r2.xyz * r0.www;
  r0.z = saturate(r0.z);
  r3.xy = v0.xy * g_screen_size.zw + -g_viewport_offset_scale.xy;
  r3.xy = r3.xy / g_viewport_offset_scale.zw;
  r4.xy = r3.xy * float2(2,-2) + float2(-1,1);
  r4.w = 1;
  r3.x = dot(r4.xyzw, inv_view_projection._m00_m10_m20_m30);
  r3.y = dot(r4.xyzw, inv_view_projection._m01_m11_m21_m31);
  r3.z = dot(r4.xyzw, inv_view_projection._m02_m12_m22_m32);
  r0.w = dot(r4.xyzw, inv_view_projection._m03_m13_m23_m33);
  r3.xyz = r3.xyz / r0.www;
  r4.xyz = -v3.xyz + r3.xyz;
  r7.xyz = g_point_light_shadow_map_sample_bias * r4.xyz;
  r0.w = max(abs(r7.x), abs(r7.y));
  r0.w = max(r0.w, abs(r7.z));
  r0.w = g_light_proj_zw.y / r0.w;
  r0.w = g_light_proj_zw.x + r0.w;
  r2.w = dot(r7.xyz, r7.xyz);
  r2.w = rsqrt(r2.w);
  r7.xyz = r7.xyz * r2.www;
  r8.xzw = -r7.zxy;
  r9.xyzw = cmp(abs(r7.yzxz) < abs(r7.xxyy));
  r10.xyz = cmp(float3(0,0,0) < r7.xyz);
  r8.y = 0;
  r11.xw = float2(1,2);
  r11.yz = float2(1,-1) * r7.zy;
  r12.xzw = r10.xxx ? r8.yxw : r11.xyz;
  r9.xy = r9.yw ? r9.xz : 0;
  r10.x = 3;
  r10.w = r8.x;
  r11.xw = r10.yy ? r11.wy : r10.xw;
  r9.zw = cmp(abs(r7.yx) < abs(r7.zz));
  r2.w = r9.w ? r9.z : 0;
  r13.x = 4;
  r13.zw = float2(1,-1) * r7.xy;
  r8.x = 5;
  r8.xzw = r10.zzz ? r13.xzw : r8.xzw;
  r8.y = abs(r7.z);
  r8.xyzw = r2.wwww ? r8.xyzw : float4(0,1,0,0);
  r11.y = abs(r7.y);
  r11.z = r13.z;
  r8.xyzw = r9.yyyy ? r11.xyzw : r8.xyzw;
  r12.y = abs(r7.x);
  r7.xyzw = r9.xxxx ? r12.xyzw : r8.xyzw;
  r7.yz = r7.zw / r7.yy;
  r7.yz = float2(1,1) + r7.yz;
  r8.xy = float2(0.5,0.5) * r7.yz;
  r7.w = (int)r7.x;
  r2.w = cmp((int)r0.x == -1);
  if (r2.w != 0) {
    r9.xy = g_shadow_buffer_size.xy * r8.xy + float2(0.5,0.5);
    r9.zw = floor(r9.xy);
    r9.xy = r9.xy + -r9.zw;
    r10.xy = -r9.xy * g_shadow_buffer_size.zw + r8.xy;
    r11.y = -3 * g_shadow_buffer_size.z;
    r9.zw = float2(0,0);
    r2.w = -3;
    while (true) {
      r3.w = cmp(3 < (int)r2.w);
      if (r3.w != 0) break;
      r3.w = (int)r2.w;
      r11.z = g_shadow_buffer_size.w * r3.w;
      r7.yz = r11.yz + r10.xy;
      r12.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r7.yzw).xyzw;
      r13.xyzw = cmp(r12.xyzw >= r0.wwww);
      r13.xyzw = r13.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r3.w = dot(r13.xyzw, float4(1,1,1,1));
      r4.w = dot(r12.xyzw, r13.xyzw);
      r7.x = g_shadow_buffer_size.z * 2 + r7.y;
      r12.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r7.xzw).xyzw;
      r13.xyzw = cmp(r12.xyzw >= r0.wwww);
      r13.xyzw = r13.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r13.xyzw, float4(1,1,1,1));
      r3.w = r5.w + r3.w;
      r5.w = dot(r12.xyzw, r13.xyzw);
      r4.w = r5.w + r4.w;
      r12.y = g_shadow_buffer_size.z * 2 + r7.x;
      r12.zw = r7.zw;
      r13.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r12.yzw).xyzw;
      r14.xyzw = cmp(r13.xyzw >= r0.wwww);
      r14.xyzw = r14.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r14.xyzw, float4(1,1,1,1));
      r3.w = r5.w + r3.w;
      r5.w = dot(r13.xyzw, r14.xyzw);
      r4.w = r5.w + r4.w;
      r12.x = g_shadow_buffer_size.z * 2 + r12.y;
      r12.xyzw = t_point_light_shadow_map.Gather(s_point_light_shadow_map_s, r12.xzw).xyzw;
      r13.xyzw = cmp(r12.xyzw >= r0.wwww);
      r13.xyzw = r13.xyzw ? float4(0,0,0,0) : float4(1,1,1,1);
      r5.w = dot(r13.xyzw, float4(1,1,1,1));
      r3.w = r5.w + r3.w;
      r5.w = dot(r12.xyzw, r13.xyzw);
      r4.w = r5.w + r4.w;
      r9.w = r9.w + r3.w;
      r9.z = r9.z + r4.w;
      r2.w = (int)r2.w + 2;
    }
    r2.w = cmp(r9.w < 64);
    if (r2.w != 0) {
      r2.w = cmp(0 < r9.w);
      r3.w = r9.z / r9.w;
      r3.w = r0.w / r3.w;
      r3.w = -1 + r3.w;
      r7.y = saturate(g_light_width * r3.w);
      r7.x = 0;
      r7.xy = r2.ww ? r7.xy : float2(1,0);
      if (r2.w != 0) {
        r2.w = r7.y * r7.y;
        r3.w = r2.w * r7.y;
        r4.w = 3 * r2.w;
        r5.w = 1 + -r7.y;
        r6.w = r5.w * r4.w;
        r7.z = r5.w * r5.w;
        r8.w = r7.z * r7.y;
        r9.z = 3 * r8.w;
        r9.w = r7.z * r5.w;
        r10.w = 0;
        r11.x = 0;
        while (true) {
          r11.y = cmp((int)r10.w >= 11);
          if (r11.y != 0) break;
          r11.y = r2.w * r7.y + r11.x;
          r11.z = (int)r10.w * 11;
          r11.w = r6.w * icb[r11.z+1].x + r3.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+2].x * r6.w;
          r11.w = r9.z * icb[r11.z+2].y + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+3].x * r6.w;
          r11.w = r9.z * icb[r11.z+3].y + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+4].y * r9.z;
          r11.w = r9.w * icb[r11.z+4].z + r11.w;
          r11.w = r6.w * icb[r11.z+4].x + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+5].y * r9.z;
          r11.w = r9.w * icb[r11.z+5].z + r11.w;
          r11.w = r6.w * icb[r11.z+5].x + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+6].y * r9.z;
          r11.w = r9.w * icb[r11.z+6].z + r11.w;
          r11.w = r6.w * icb[r11.z+6].x + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+7].x * r6.w;
          r11.w = r9.z * icb[r11.z+7].y + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.w = icb[r11.z+8].x * r6.w;
          r11.w = r9.z * icb[r11.z+8].y + r11.w;
          r11.w = r2.w * r7.y + r11.w;
          r11.y = r11.y + r11.w;
          r11.z = r6.w * icb[r11.z+9].x + r3.w;
          r11.y = r11.y + r11.z;
          r11.x = r2.w * r7.y + r11.y;
          r10.w = (int)r10.w + 1;
        }
        r10.z = r7.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,-5)).xyzw;
        r10.w = 1 + -r9.y;
        r11.y = -r3.w * r9.x + r3.w;
        r11.zw = r12.wx * r11.yy;
        r12.xz = r12.zy * r3.ww + r11.zw;
        r11.z = r12.z * r9.y;
        r11.z = r10.w * r12.x + r11.z;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,-5)).xyzw;
        r12.x = dot(r13.wz, r3.ww);
        r11.z = r10.w * r12.x + r11.z;
        r12.x = dot(r13.xy, r3.ww);
        r11.z = r9.y * r12.x + r11.z;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,-5)).xyzw;
        r12.x = dot(r14.wz, r3.ww);
        r11.z = r10.w * r12.x + r11.z;
        r12.x = dot(r14.xy, r3.ww);
        r11.z = r9.y * r12.x + r11.z;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,-5)).xyzw;
        r12.x = dot(r15.wz, r3.ww);
        r11.z = r10.w * r12.x + r11.z;
        r12.x = dot(r15.xy, r3.ww);
        r11.z = r9.y * r12.x + r11.z;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,-5)).xyzw;
        r12.x = dot(r16.wz, r3.ww);
        r11.z = r10.w * r12.x + r11.z;
        r12.x = dot(r16.xy, r3.ww);
        r11.z = r9.y * r12.x + r11.z;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,-5)).xyzw;
        r12.xz = r17.zy * r9.xx;
        r12.xz = r12.xz * r3.ww;
        r12.xw = r17.wx * r3.ww + r12.xz;
        r11.z = r10.w * r12.x + r11.z;
        r11.z = r9.y * r12.w + r11.z;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,-3)).xyzw;
        r12.x = 0.200000003 * r6.w;
        r12.w = r6.w * 0.200000003 + r3.w;
        r13.z = r2.w * r7.y + -r12.w;
        r13.z = r9.x * r13.z + r12.w;
        r14.zw = r18.yz * r13.zz;
        r14.zw = r18.xw * r11.yy + r14.zw;
        r11.z = r10.w * r14.w + r11.z;
        r11.z = r9.y * r14.z + r11.z;
        r11.w = r12.y * r13.z + r11.w;
        r11.z = r10.w * r11.w + r11.z;
        r11.z = r9.y * r14.w + r11.z;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,-3)).xyzw;
        r15.zw = r8.ww * float2(3,0.600000024) + r6.ww;
        r15.zw = r2.ww * r7.yy + r15.zw;
        r6.w = -r15.w + r12.w;
        r6.w = r9.x * r6.w + r15.w;
        r16.zw = r18.wx * r6.ww;
        r17.yz = r18.zy * r15.ww + r16.zw;
        r8.w = r10.w * r17.y + r11.z;
        r8.w = r9.y * r17.z + r8.w;
        r11.z = dot(r13.xy, r12.ww);
        r8.w = r10.w * r11.z + r8.w;
        r11.z = dot(r18.wz, r12.ww);
        r8.w = r9.y * r11.z + r8.w;
        r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,-3)).xyzw;
        r11.z = dot(r19.wz, r15.ww);
        r8.w = r10.w * r11.z + r8.w;
        r11.z = dot(r19.xy, r15.ww);
        r8.w = r9.y * r11.z + r8.w;
        r11.z = dot(r14.xy, r12.ww);
        r8.w = r10.w * r11.z + r8.w;
        r11.z = dot(r19.wz, r12.ww);
        r8.w = r9.y * r11.z + r8.w;
        r20.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,-3)).xyzw;
        r11.z = dot(r20.wz, r15.ww);
        r8.w = r10.w * r11.z + r8.w;
        r11.z = dot(r20.xy, r15.ww);
        r8.w = r9.y * r11.z + r8.w;
        r11.z = dot(r15.xy, r12.ww);
        r8.w = r10.w * r11.z + r8.w;
        r11.z = dot(r20.wz, r12.ww);
        r8.w = r9.y * r11.z + r8.w;
        r21.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,-3)).xyzw;
        r11.z = r15.w + -r12.w;
        r11.z = r9.x * r11.z + r12.w;
        r13.xy = r21.zy * r11.zz;
        r13.xw = r21.wx * r15.ww + r13.xy;
        r8.w = r10.w * r13.x + r8.w;
        r8.w = r9.y * r13.w + r8.w;
        r11.w = dot(r16.xy, r12.ww);
        r8.w = r10.w * r11.w + r8.w;
        r11.w = dot(r21.wz, r12.ww);
        r8.w = r9.y * r11.w + r8.w;
        r22.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,-3)).xyzw;
        r11.w = r9.x * r12.x + r3.w;
        r12.xy = r22.yz * r9.xx;
        r12.xy = r12.xy * r3.ww;
        r12.xy = r22.xw * r11.ww + r12.xy;
        r8.w = r10.w * r12.y + r8.w;
        r8.w = r9.y * r12.x + r8.w;
        r12.z = r17.x * r11.w + r12.z;
        r8.w = r10.w * r12.z + r8.w;
        r8.w = r9.y * r12.y + r8.w;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,-1)).xyzw;
        r12.yz = r17.yz * r13.zz;
        r12.yz = r17.xw * r11.yy + r12.yz;
        r8.w = r10.w * r12.z + r8.w;
        r8.w = r9.y * r12.y + r8.w;
        r8.w = r10.w * r14.z + r8.w;
        r8.w = r9.y * r12.z + r8.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,-1)).xyzw;
        r13.xw = r15.wz + -r15.zw;
        r13.xw = r9.xx * r13.xw + r15.zw;
        r14.yz = r14.yz * r13.xx;
        r14.xy = r14.xw * r6.ww + r14.yz;
        r8.w = r10.w * r14.y + r8.w;
        r8.w = r9.y * r14.x + r8.w;
        r12.z = r18.y * r13.x + r16.w;
        r8.w = r10.w * r12.z + r8.w;
        r8.w = r9.y * r14.y + r8.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,-1)).xyzw;
        r9.w = r9.w * 0.800000012 + r9.z;
        r9.w = r4.w * r5.w + r9.w;
        r9.w = r2.w * r7.y + r9.w;
        r12.z = r15.z + -r9.w;
        r12.z = r9.x * r12.z + r9.w;
        r14.yz = r16.wx * r12.zz;
        r14.yw = r16.zy * r9.ww + r14.yz;
        r8.w = r10.w * r14.y + r8.w;
        r8.w = r9.y * r14.w + r8.w;
        r14.y = dot(r19.xy, r15.zz);
        r8.w = r10.w * r14.y + r8.w;
        r14.y = dot(r16.wz, r15.zz);
        r8.w = r9.y * r14.y + r8.w;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,-1)).xyzw;
        r14.y = r9.w + -r15.z;
        r14.y = r9.x * r14.y + r15.z;
        r15.xy = r17.zy * r14.yy;
        r16.xz = r17.wx * r9.ww + r15.xy;
        r8.w = r10.w * r16.x + r8.w;
        r8.w = r9.y * r16.z + r8.w;
        r14.w = dot(r20.xy, r15.zz);
        r8.w = r10.w * r14.w + r8.w;
        r14.w = dot(r17.wz, r15.zz);
        r8.w = r9.y * r14.w + r8.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,-1)).xyzw;
        r16.xz = r18.yz * r11.zz;
        r16.xz = r18.xw * r13.ww + r16.xz;
        r8.w = r10.w * r16.z + r8.w;
        r8.w = r9.y * r16.x + r8.w;
        r13.y = r21.x * r13.w + r13.y;
        r8.w = r10.w * r13.y + r8.w;
        r8.w = r9.y * r16.z + r8.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,-1)).xyzw;
        r16.zw = r18.yz * r9.xx;
        r16.zw = r16.zw * r3.ww;
        r16.zw = r18.xw * r11.ww + r16.zw;
        r8.w = r10.w * r16.w + r8.w;
        r8.w = r9.y * r16.z + r8.w;
        r8.w = r10.w * r12.x + r8.w;
        r8.w = r9.y * r16.w + r8.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,1)).xyzw;
        r17.yz = r18.yz * r13.zz;
        r17.yz = r18.xw * r11.yy + r17.yz;
        r8.w = r10.w * r17.z + r8.w;
        r8.w = r9.y * r17.y + r8.w;
        r8.w = r10.w * r12.y + r8.w;
        r8.w = r9.y * r17.z + r8.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,1)).xyzw;
        r12.xy = r18.yz * r13.xx;
        r12.xy = r18.xw * r6.ww + r12.xy;
        r8.w = r10.w * r12.y + r8.w;
        r8.w = r9.y * r12.x + r8.w;
        r8.w = r10.w * r14.x + r8.w;
        r8.w = r9.y * r12.y + r8.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,1)).xyzw;
        r12.yz = r18.wx * r12.zz;
        r14.xw = r18.zy * r9.ww + r12.yz;
        r8.w = r10.w * r14.x + r8.w;
        r8.w = r9.y * r14.w + r8.w;
        r7.z = r7.z * r5.w + r9.z;
        r4.w = r4.w * r5.w + r7.z;
        r2.w = r2.w * r7.y + r4.w;
        r4.w = r9.w + -r2.w;
        r4.w = r9.x * r4.w + r2.w;
        r5.w = r16.y * r4.w + r14.z;
        r5.w = r10.w * r5.w + r8.w;
        r4.w = r18.z * r4.w + r12.y;
        r4.w = r9.y * r4.w + r5.w;
        r19.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,1)).xyzw;
        r7.yz = r19.zy * r14.yy;
        r12.yz = r19.wx * r9.ww + r7.yz;
        r4.w = r10.w * r12.y + r4.w;
        r4.w = r9.y * r12.z + r4.w;
        r2.w = r2.w + -r9.w;
        r2.w = r9.x * r2.w + r9.w;
        r5.w = r17.x * r2.w + r15.y;
        r4.w = r10.w * r5.w + r4.w;
        r2.w = r19.w * r2.w + r7.y;
        r2.w = r9.y * r2.w + r4.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,1)).xyzw;
        r7.yz = r14.yz * r11.zz;
        r7.yz = r14.xw * r13.ww + r7.yz;
        r2.w = r10.w * r7.z + r2.w;
        r2.w = r9.y * r7.y + r2.w;
        r2.w = r10.w * r16.x + r2.w;
        r2.w = r9.y * r7.z + r2.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,1)).xyzw;
        r9.zw = r14.yz * r9.xx;
        r9.zw = r9.zw * r3.ww;
        r9.zw = r14.xw * r11.ww + r9.zw;
        r2.w = r10.w * r9.w + r2.w;
        r2.w = r9.y * r9.z + r2.w;
        r2.w = r10.w * r16.z + r2.w;
        r2.w = r9.y * r9.w + r2.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,3)).xyzw;
        r12.yz = r14.yz * r13.zz;
        r12.yz = r14.xw * r11.yy + r12.yz;
        r2.w = r10.w * r12.z + r2.w;
        r2.w = r9.y * r12.y + r2.w;
        r2.w = r10.w * r17.y + r2.w;
        r2.w = r9.y * r12.z + r2.w;
        r14.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,3)).xyzw;
        r15.xy = r14.wx * r6.ww;
        r16.xy = r14.zy * r15.ww + r15.xy;
        r2.w = r10.w * r16.x + r2.w;
        r2.w = r9.y * r16.y + r2.w;
        r2.w = r10.w * r12.x + r2.w;
        r4.w = r14.z * r13.x + r15.x;
        r2.w = r9.y * r4.w + r2.w;
        r16.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,3)).xyzw;
        r4.w = dot(r16.wz, r15.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r16.xy, r15.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r18.xy, r15.zz);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r16.wz, r15.zz);
        r2.w = r9.y * r4.w + r2.w;
        r17.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,3)).xyzw;
        r4.w = dot(r17.wz, r15.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r17.xy, r15.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r19.xy, r15.zz);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r17.wz, r15.zz);
        r2.w = r9.y * r4.w + r2.w;
        r18.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,3)).xyzw;
        r12.xz = r18.zy * r11.zz;
        r13.xy = r18.wx * r15.ww + r12.xz;
        r2.w = r10.w * r13.x + r2.w;
        r2.w = r9.y * r13.y + r2.w;
        r2.w = r10.w * r7.y + r2.w;
        r4.w = r18.w * r13.w + r12.x;
        r2.w = r9.y * r4.w + r2.w;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,3)).xyzw;
        r7.yz = r15.yz * r9.xx;
        r7.yz = r7.yz * r3.ww;
        r7.yz = r15.xw * r11.ww + r7.yz;
        r2.w = r10.w * r7.z + r2.w;
        r2.w = r9.y * r7.y + r2.w;
        r2.w = r10.w * r9.z + r2.w;
        r2.w = r9.y * r7.z + r2.w;
        r15.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-5,5)).xyzw;
        r9.zw = r15.wx * r11.yy;
        r11.yz = r15.zy * r3.ww + r9.zw;
        r2.w = r10.w * r11.y + r2.w;
        r2.w = r9.y * r11.z + r2.w;
        r2.w = r10.w * r12.y + r2.w;
        r4.w = r15.z * r13.z + r9.z;
        r2.w = r9.y * r4.w + r2.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-3,5)).xyzw;
        r4.w = dot(r13.wz, r3.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.xy, r3.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r14.xy, r12.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.wz, r12.ww);
        r2.w = r9.y * r4.w + r2.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(-1,5)).xyzw;
        r4.w = dot(r13.wz, r3.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.xy, r3.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r16.xy, r12.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.wz, r12.ww);
        r2.w = r9.y * r4.w + r2.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(1,5)).xyzw;
        r4.w = dot(r13.wz, r3.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.xy, r3.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r17.xy, r12.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.wz, r12.ww);
        r2.w = r9.y * r4.w + r2.w;
        r13.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(3,5)).xyzw;
        r4.w = dot(r13.wz, r3.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.xy, r3.ww);
        r2.w = r9.y * r4.w + r2.w;
        r4.w = dot(r18.xy, r12.ww);
        r2.w = r10.w * r4.w + r2.w;
        r4.w = dot(r13.wz, r12.ww);
        r2.w = r9.y * r4.w + r2.w;
        r12.xyzw = t_point_light_shadow_map.GatherCmp(s_point_light_shadow_map_sm50_s, r10.xyz, r0.w, int2(5,5)).xyzw;
        r9.xz = r12.zy * r9.xx;
        r9.xz = r9.xz * r3.ww;
        r9.zw = r12.wx * r3.ww + r9.xz;
        r2.w = r10.w * r9.z + r2.w;
        r2.w = r9.y * r9.w + r2.w;
        r2.w = r10.w * r7.y + r2.w;
        r3.w = r12.w * r11.w + r9.x;
        r2.w = r9.y * r3.w + r2.w;
        r7.x = r2.w / r11.x;
      }
    } else {
      r7.x = 0;
    }
  } else {
    r7.x = 1;
  }
  if (r0.x == 0) {
    r8.z = r7.w;
    r0.x = t_point_light_shadow_map.Sample(s_point_light_shadow_map_s, r8.xyz).x;
    r0.x = cmp(r0.w < r0.x);
    r7.x = r0.x ? 1.000000 : 0;
  }
  r3.xyz = -camera_position.xyz + r3.xyz;
  r0.x = dot(r3.xyz, r3.xyz);
  r0.x = rsqrt(r0.x);
  r7.yzw = r3.xyz * r0.xxx;
  r0.w = dot(r4.xyz, r4.xyz);
  r2.w = rsqrt(r0.w);
  r4.xyz = r4.xyz * r2.www;
  r0.w = sqrt(r0.w);
  r0.w = saturate(r0.w / v3.w);
  r2.w = dot(r7.yzw, r5.xyz);
  r3.w = r2.w + r2.w;
  r8.xyz = r5.xyz * -r3.www + r7.yzw;
  r0.y = (int)r0.y & 9;
  r0.y = cmp((int)r0.y == 9);
  r3.w = -0.5 + r0.z;
  r3.w = r3.w + r3.w;
  r3.w = max(0, r3.w);
  r4.w = r3.w * -2 + 3;
  r3.w = r3.w * r3.w;
  r3.w = r4.w * r3.w;
  r3.w = r0.y ? r3.w : 0;
  r1.xyzw = r0.yyyy ? r1.xyzw : 0;
  r0.y = dot(-r4.xyz, r8.xyz);
  r0.y = max(-1, r0.y);
  r0.y = min(1, r0.y);
  r4.w = 1 + -abs(r0.y);
  r4.w = sqrt(r4.w);
  r5.w = abs(r0.y) * -0.0187292993 + 0.0742610022;
  r5.w = r5.w * abs(r0.y) + -0.212114394;
  r5.w = r5.w * abs(r0.y) + 1.57072878;
  r6.w = r5.w * r4.w;
  r6.w = r6.w * -2 + 3.14159274;
  r0.y = cmp(r0.y < -r0.y);
  r0.y = r0.y ? r6.w : 0;
  r0.y = r5.w * r4.w + r0.y;
  r4.w = r0.z * -1.12524891 + 1.12839997;
  r8.xyz = float3(-0.00872664619,0.00872664619,0.00872664619) + r0.yyy;
  r0.y = 1 / r4.w;
  r8.xyz = r8.xyz * r0.yyy;
  r8.yw = float2(0.707106769,0.707106769) * r8.xy;
  r5.w = abs(r8.y) * abs(r8.y);
  r6.w = r5.w * abs(r8.y);
  r9.x = r6.w * abs(r8.y);
  r8.y = abs(r8.y) * 0.278393 + 1;
  r5.w = r5.w * 0.230388999 + r8.y;
  r5.w = r6.w * 0.000972000009 + r5.w;
  r5.w = r9.x * 0.0781079978 + r5.w;
  r5.w = r5.w * r5.w;
  r5.w = r5.w * r5.w;
  r5.w = 1 / r5.w;
  r5.w = 1 + -r5.w;
  r6.w = cmp(0 < r8.x);
  r8.x = cmp(r8.x < 0);
  r6.w = (int)-r6.w + (int)r8.x;
  r6.w = (int)r6.w;
  r5.w = r6.w * r5.w + 1;
  r5.w = 0.5 * r5.w;
  r6.w = r8.w * r8.w;
  r8.x = r6.w * r8.w;
  r8.y = r8.x * r8.w;
  r8.z = r8.z * 0.196853578 + 1;
  r6.w = r6.w * 0.230388999 + r8.z;
  r6.w = r8.x * 0.000972000009 + r6.w;
  r6.w = r8.y * 0.0781079978 + r6.w;
  r6.w = r6.w * r6.w;
  r6.w = r6.w * r6.w;
  r6.w = 1 / r6.w;
  r6.w = 2 + -r6.w;
  r5.w = r6.w * 0.5 + -r5.w;
  r8.xy = float2(-1.11072075,1.11072075) * r0.yy;
  r8.zw = r8.xy * r8.xy;
  r6.w = r8.z * -r8.x;
  r8.x = r6.w * -r8.x;
  r0.y = r0.y * 0.309216887 + 1;
  r9.xy = r8.zw * float2(0.230388999,0.230388999) + r0.yy;
  r0.y = r6.w * 0.000972000009 + r9.x;
  r0.y = r8.x * 0.0781079978 + r0.y;
  r0.y = r0.y * r0.y;
  r0.y = r0.y * r0.y;
  r0.y = 1 / r0.y;
  r0.y = 1 + -r0.y;
  r0.y = 1 + -r0.y;
  r0.y = 0.5 * r0.y;
  r6.w = r8.w * r8.y;
  r8.x = r6.w * r8.y;
  r6.w = r6.w * 0.000972000009 + r9.y;
  r6.w = r8.x * 0.0781079978 + r6.w;
  r6.w = r6.w * r6.w;
  r6.w = r6.w * r6.w;
  r6.w = 1 / r6.w;
  r6.w = 2 + -r6.w;
  r0.y = r6.w * 0.5 + -r0.y;
  r0.y = abs(r5.w) / r0.y;
  r0.y = min(1, r0.y);
  r0.y = r0.y * r0.y;
  r0.zw = float2(1,1) + -r0.zw;
  r5.w = dot(r5.xyz, -r4.xyz);
  r6.w = saturate(r5.w);
  r8.x = 1.57079637 * r0.z;
  r8.x = sin(r8.x);
  r8.y = -1 + r6.w;
  r8.x = r8.x * r8.y + 1;
  r5.x = saturate(dot(r5.xyz, -r7.yzw));
  r0.z = r0.z * r0.z;
  r5.y = 0.5 * r0.z;
  r0.z = -r0.z * 0.5 + 1;
  r5.z = r6.w * r0.z + r5.y;
  r5.z = 1 / r5.z;
  r0.z = r5.x * r0.z + r5.y;
  r0.z = 1 / r0.z;
  r0.z = r5.z * r0.z;
  r0.yz = float2(0.785398185,0.25) * r0.yz;
  r0.z = min(1, r0.z);
  r0.z = r0.z * r8.x;
  r5.x = cmp(0 < r3.w);
  r5.y = cmp(0.00100000005 < r1.x);
  r5.x = r5.y ? r5.x : 0;
  if (r5.x != 0) {
    r8.xyz = r3.xyz * r0.xxx + -r4.xyz;
    r5.y = dot(r8.xyz, r8.xyz);
    r5.y = rsqrt(r5.y);
    r8.xyz = r8.xyz * r5.yyy;
    r5.y = saturate(dot(r7.yzw, r8.xyz));
    r5.z = r5.y * -5.55472994 + -6.98316002;
    r5.y = r5.z * r5.y;
    r5.y = exp2(r5.y);
    r9.xyz = float3(1,1,1) + -r6.xyz;
    r9.xyz = r9.xyz * r5.yyy + r6.xyz;
    r5.y = cmp(r5.w < 0);
    r5.z = cmp(r2.w < 0);
    r5.y = r5.z ? r5.y : 0;
    if (r5.y != 0) {
      r10.xyz = float3(0,0,0);
    } else {
      r11.xyz = float3(333.333374,10,4) * r1.xxy;
      r5.y = r1.z * 4 + 1;
      r11.x = saturate(r11.x);
      r5.z = r11.x * -2 + 3;
      r8.w = r11.x * r11.x;
      r5.z = r8.w * r5.z;
      r5.z = r5.z * r11.z + 1;
      r8.x = dot(r8.xyz, -r4.xyz);
      r8.y = 1 / r5.z;
      r8.y = r8.y * r8.y;
      r8.z = -r8.x * r8.x + 1;
      r8.y = -r8.y * r8.z + 1;
      r8.w = max(9.99999975e-005, r5.z);
      r8.w = 1 / r8.w;
      r8.w = r8.w * r8.w;
      r9.w = r8.w * r8.z;
      r9.w = cmp(1 < r9.w);
      r10.w = 1 / r8.w;
      r10.w = -r10.w + r8.z;
      r10.w = max(0, r10.w);
      r10.w = sqrt(r10.w);
      r11.x = r10.w * -r8.w;
      r11.z = r11.x / r8.x;
      r11.w = -r10.w / r8.x;
      r12.xy = min(float2(1,1), abs(r11.zw));
      r12.zw = max(float2(1,1), abs(r11.zw));
      r12.zw = float2(1,1) / r12.zw;
      r12.xy = r12.xy * r12.zw;
      r12.zw = r12.xy * r12.xy;
      r13.xy = r12.zw * float2(0.0208350997,0.0208350997) + float2(-0.0851330012,-0.0851330012);
      r13.xy = r12.zw * r13.xy + float2(0.180141002,0.180141002);
      r13.xy = r12.zw * r13.xy + float2(-0.330299497,-0.330299497);
      r12.zw = r12.zw * r13.xy + float2(0.999866009,0.999866009);
      r13.xy = r12.xy * r12.zw;
      r13.zw = cmp(float2(1,1) < abs(r11.zw));
      r13.xy = r13.xy * float2(-2,-2) + float2(1.57079637,1.57079637);
      r13.xy = r13.zw ? r13.xy : 0;
      r12.xy = r12.xy * r12.zw + r13.xy;
      r11.xz = min(float2(1,1), r11.zw);
      r11.xz = cmp(r11.xz < -r11.xz);
      r11.xz = r11.xz ? -r12.xy : r12.xy;
      r12.zw = r11.xz + r11.xz;
      r8.z = -r8.w * r8.z + 1;
      r8.yz = sqrt(r8.yz);
      r8.w = r5.z * r8.x + -r8.z;
      r10.w = r5.z * r8.x + r8.z;
      r13.x = r8.w / r10.w;
      r8.w = -r5.z * r8.z + r8.x;
      r8.x = r5.z * r8.z + r8.x;
      r13.y = r8.w / r8.x;
      r8.xz = cmp(r13.xy < float2(0,0));
      r14.zw = r8.xz ? float2(3.14159298,3.14159298) : 0;
      r14.xy = r13.xy * r13.xy;
      r12.xy = float2(1,1);
      r12.xyzw = r9.wwww ? r12.xyzw : r14.xyzw;
      r13.xyzw = float4(1,1,3.14159274,3.14159274) + -r12.xyzw;
      r8.x = cmp(r1.w == 0.000000);
      if (r8.x != 0) {
        r8.x = -r8.y * r8.y + 1;
        r8.z = max(9.99999975e-005, r5.y);
        r8.z = r5.z / r8.z;
        r8.z = r8.z * r8.z;
        r8.w = r8.z * r8.x;
        r8.w = cmp(1 < r8.w);
        r9.w = 1 / r8.z;
        r9.w = -r9.w + r8.x;
        r9.w = max(0, r9.w);
        r9.w = sqrt(r9.w);
        r10.w = r9.w * -r8.z;
        r11.z = r10.w / r8.y;
        r11.w = -r9.w / r8.y;
        r12.zw = min(float2(1,1), -r11.zw);
        r14.xy = max(float2(1,1), -r11.zw);
        r14.xy = float2(1,1) / r14.xy;
        r12.zw = r14.xy * r12.zw;
        r14.xy = r12.zw * r12.zw;
        r14.zw = r14.xy * float2(0.0208350997,0.0208350997) + float2(-0.0851330012,-0.0851330012);
        r14.zw = r14.xy * r14.zw + float2(0.180141002,0.180141002);
        r14.zw = r14.xy * r14.zw + float2(-0.330299497,-0.330299497);
        r14.xy = r14.xy * r14.zw + float2(0.999866009,0.999866009);
        r14.zw = r14.xy * r12.zw;
        r15.xy = cmp(float2(1,1) < -r11.zw);
        r14.zw = r14.zw * float2(-2,-2) + float2(1.57079637,1.57079637);
        r14.zw = r15.xy ? r14.zw : 0;
        r12.zw = r12.zw * r14.xy + r14.zw;
        r11.xz = cmp(r11.zw < -r11.zw);
        r11.xz = r11.xz ? -r12.zw : r12.zw;
        r14.zw = r11.xz + r11.xz;
        r8.x = -r8.z * r8.x + 1;
        r8.x = sqrt(r8.x);
        r8.z = r8.x * r5.z;
        r9.w = r5.y * r8.y + -r8.z;
        r8.z = r5.y * r8.y + r8.z;
        r15.x = r9.w / r8.z;
        r8.x = r8.x * r5.y;
        r8.z = r5.z * r8.y + -r8.x;
        r8.x = r5.z * r8.y + r8.x;
        r15.y = r8.z / r8.x;
        r8.xz = cmp(r15.xy < float2(0,0));
        r16.zw = r8.xz ? float2(3.14159298,3.14159298) : 0;
        r16.xy = r15.xy * r15.xy;
        r14.xy = float2(1,1);
        r14.xyzw = r8.wwww ? r14.xyzw : r16.xyzw;
      } else {
        r8.x = 5 * r1.w;
        r5.y = r5.y * r5.y;
        r8.z = -r8.x * r8.x + 1;
        r8.w = r8.z * r5.y;
        r9.w = r5.z * r5.z;
        r10.w = -r8.y * r8.y + 1;
        r10.w = -r9.w * r10.w + r8.w;
        r11.x = dot(r8.xx, r5.yy);
        r11.z = r11.x * r11.x;
        r11.z = r10.w * r10.w + r11.z;
        r11.z = sqrt(r11.z);
        r11.w = r11.z + r10.w;
        r11.w = 0.5 * r11.w;
        r11.w = sqrt(r11.w);
        r10.w = r11.z + -r10.w;
        r10.w = 0.5 * r10.w;
        r10.w = sqrt(r10.w);
        r11.z = r8.y * r5.z;
        r12.z = r5.z * r8.y + -r11.w;
        r12.w = r10.w * r10.w;
        r12.z = r12.z * r12.z + r12.w;
        r15.x = r5.z * r8.y + r11.w;
        r15.x = r15.x * r15.x + r12.w;
        r14.y = r12.z / r15.x;
        r12.z = r5.z + r5.z;
        r15.x = r12.z * r10.w;
        r15.x = r15.x * r8.y;
        r12.w = r11.w * r11.w + r12.w;
        r11.z = -r11.z * r11.z + r12.w;
        r15.y = min(abs(r15.x), abs(r11.z));
        r15.z = max(abs(r15.x), abs(r11.z));
        r15.z = 1 / r15.z;
        r15.y = r15.y * r15.z;
        r15.z = r15.y * r15.y;
        r15.w = r15.z * 0.0208350997 + -0.0851330012;
        r15.w = r15.z * r15.w + 0.180141002;
        r15.w = r15.z * r15.w + -0.330299497;
        r15.z = r15.z * r15.w + 0.999866009;
        r15.w = r15.y * r15.z;
        r16.x = cmp(abs(r11.z) < abs(r15.x));
        r15.w = r15.w * -2 + 1.57079637;
        r15.w = r16.x ? r15.w : 0;
        r15.y = r15.y * r15.z + r15.w;
        r15.z = cmp(r11.z < -r11.z);
        r15.z = r15.z ? -3.141593 : 0;
        r15.y = r15.y + r15.z;
        r15.z = min(r15.x, r11.z);
        r11.z = max(r15.x, r11.z);
        r15.x = cmp(r15.z < -r15.z);
        r11.z = cmp(r11.z >= -r11.z);
        r11.z = r11.z ? r15.x : 0;
        r11.z = r11.z ? -r15.y : r15.y;
        r14.w = 3.14159274 + r11.z;
        r11.z = r11.w * r5.z;
        r15.x = r8.w * r8.y + -r11.z;
        r5.z = r10.w * r5.z;
        r15.y = r11.x * r8.y + -r5.z;
        r15.y = r15.y * r15.y;
        r15.x = r15.x * r15.x + r15.y;
        r8.w = r8.w * r8.y + r11.z;
        r5.z = r11.x * r8.y + r5.z;
        r5.z = r5.z * r5.z;
        r5.z = r8.w * r8.w + r5.z;
        r14.x = r15.x / r5.z;
        r5.z = r12.z * r5.y;
        r5.z = r5.z * r8.y;
        r8.w = r11.w * r1.w;
        r8.z = r10.w * r8.z;
        r8.z = r8.w * 10 + -r8.z;
        r8.x = r8.x * r8.x + 1;
        r5.yz = r8.xz * r5.yz;
        r5.y = r5.y * r8.y;
        r8.x = r12.w * r9.w;
        r5.y = r5.y * r5.y + -r8.x;
        r8.x = min(abs(r5.z), abs(r5.y));
        r8.z = max(abs(r5.z), abs(r5.y));
        r8.z = 1 / r8.z;
        r8.x = r8.x * r8.z;
        r8.z = r8.x * r8.x;
        r8.w = r8.z * 0.0208350997 + -0.0851330012;
        r8.w = r8.z * r8.w + 0.180141002;
        r8.w = r8.z * r8.w + -0.330299497;
        r8.z = r8.z * r8.w + 0.999866009;
        r8.w = r8.x * r8.z;
        r9.w = cmp(abs(r5.y) < abs(r5.z));
        r8.w = r8.w * -2 + 1.57079637;
        r8.w = r9.w ? r8.w : 0;
        r8.x = r8.x * r8.z + r8.w;
        r8.z = cmp(r5.y < -r5.y);
        r8.z = r8.z ? -3.141593 : 0;
        r8.x = r8.x + r8.z;
        r8.z = min(r5.z, r5.y);
        r5.y = max(r5.z, r5.y);
        r5.z = cmp(r8.z < -r8.z);
        r5.y = cmp(r5.y >= -r5.y);
        r5.y = r5.y ? r5.z : 0;
        r14.z = r5.y ? -r8.x : r8.x;
      }
      r5.y = r11.y * r8.y;
      r8.xy = r14.zw + r13.zw;
      r8.zw = r14.xy * r12.xy;
      r8.zw = sqrt(r8.zw);
      r11.xy = r13.xy * r13.xy;
      r11.xy = r11.xy * r14.xy;
      r11.zw = -r12.xy * r14.xy + float2(1,1);
      r11.xy = r11.xy / r11.zw;
      r11.zw = r12.xy + r11.xy;
      r5.z = r11.z + r11.w;
      r12.xyz = float3(0.50022918,0.500110924,0.500561237) * r5.zzz;
      r11.xy = r11.xy + -r13.xy;
      r13.xyz = r12.xyz;
      r11.zw = r11.xy;
      r5.z = 1;
      while (true) {
        r9.w = cmp(3 < (int)r5.z);
        if (r9.w != 0) break;
        r11.zw = r11.zw * r8.zw;
        r9.w = (int)r5.z;
        r10.w = r9.w * r5.y;
        r14.xy = r9.ww * r8.xy;
        r9.w = r10.w * r10.w;
        r15.xyzw = r10.wwww * float4(10.5620346,11.2802029,13.8757868,14.0737076) + r14.xxxx;
        r15.xyzw = cos(r15.xyzw);
        r15.xyzw = float4(9.04581441e-008,1.06873713e-007,1.06969942e-007,1.64408291e-008) * r15.xyzw;
        r16.xyzw = float4(-0.246491224,-0.529946446,-0.376594275,-0.257905096) * r9.wwww;
        r16.xyzw = exp2(r16.xyzw);
        r15.xyzw = r16.xyzw * r15.xyzw;
        r15.x = r15.x + r15.w;
        r14.xzw = r15.xyz * r11.zzz;
        r15.xyzw = r10.wwww * float4(10.5620346,11.2802029,13.8757868,14.0737076) + r14.yyyy;
        r15.xyzw = cos(r15.xyzw);
        r15.xyzw = r16.xyzw * r15.xyzw;
        r16.xy = float2(9.04581441e-008,1.64408291e-008) * r15.xw;
        r15.x = r16.x + r16.y;
        r15.xyz = r15.xyz * r11.www;
        r15.xyz = float3(18717828,2.0004437,2.00224495) * r15.xyz;
        r14.xyz = r14.xzw * float3(18717828,18717828,18717828) + r15.xyz;
        r13.xyz = r14.xyz * float3(0.5,0.5,0.5) + r13.xyz;
        r5.z = (int)r5.z + 1;
      }
      r10.x = saturate(dot(float3(2.37067437,-0.513885021,0.00529820006), r13.xyz));
      r10.y = saturate(dot(float3(-0.900040507,1.42530358,-0.0146949003), r13.xyz));
      r10.z = saturate(dot(float3(-0.470633805,0.0885813981,1.00939679), r13.xyz));
    }
    r8.xyz = r10.xyz + -r9.xyz;
    r8.xyz = r3.www * r8.xyz + r9.xyz;
  } else {
    r3.xyz = r3.xyz * r0.xxx + -r4.xyz;
    r0.x = dot(r3.xyz, r3.xyz);
    r0.x = rsqrt(r0.x);
    r3.xyz = r3.xyz * r0.xxx;
    r0.x = saturate(dot(r7.yzw, r3.xyz));
    r3.x = r0.x * -5.55472994 + -6.98316002;
    r0.x = r3.x * r0.x;
    r0.x = exp2(r0.x);
    r3.xyz = float3(1,1,1) + -r6.xyz;
    r8.xyz = r3.xyz * r0.xxx + r6.xyz;
  }
  r0.x = r0.y * r0.z;
  r0.xyz = r0.xxx * r8.xyz;
  if (r5.x != 0) {
    r1.xy = float2(333.333374,4) * r1.xy;
    r1.z = r1.z * 4 + 1;
    r1.x = saturate(r1.x);
    r3.x = r1.x * -2 + 3;
    r1.x = r1.x * r1.x;
    r1.x = r3.x * r1.x;
    r3.x = r1.x * r1.y;
    r1.xy = r1.xx * r1.yy + float2(1,2);
    r3.z = 1 + -r1.x;
    r3.xy = r3.xz / r1.yy;
    r4.xy = r3.xy * r3.xy;
    r5.xy = -r3.xy * r3.xy + float2(1,1);
    r1.y = cmp(r1.w == 0.000000);
    if (r1.y != 0) {
      r1.y = r1.z + -r1.x;
      r3.z = r1.z + r1.x;
      r8.x = r1.y / r3.z;
      r1.y = r1.x + -r1.z;
      r8.y = r1.y / r3.z;
      r7.yz = r8.xy * r8.xy;
    } else {
      r1.y = 5 * r1.w;
      r1.z = r1.z * r1.z;
      r1.w = -r1.y * r1.y + 1;
      r3.z = r1.z * r1.w;
      r1.y = dot(r1.yy, r1.zz);
      r4.z = r1.y * r1.y;
      r3.z = r3.z * r3.z + r4.z;
      r3.z = sqrt(r3.z);
      r4.z = r1.z * r1.w + r3.z;
      r4.z = 0.5 * r4.z;
      r4.z = sqrt(r4.z);
      r3.z = -r1.z * r1.w + r3.z;
      r3.z = 0.5 * r3.z;
      r3.z = sqrt(r3.z);
      r5.z = -r4.z + r1.x;
      r7.w = r3.z * r3.z;
      r5.z = r5.z * r5.z + r7.w;
      r8.x = r4.z + r1.x;
      r7.w = r8.x * r8.x + r7.w;
      r7.z = r5.z / r7.w;
      r4.z = r4.z * r1.x;
      r5.z = r1.z * r1.w + -r4.z;
      r7.w = -r1.x * r3.z + r1.y;
      r7.w = r7.w * r7.w;
      r5.z = r5.z * r5.z + r7.w;
      r1.z = r1.z * r1.w + r4.z;
      r1.x = r1.x * r3.z + r1.y;
      r1.x = r1.x * r1.x;
      r1.x = r1.z * r1.z + r1.x;
      r7.y = r5.z / r1.x;
    }
    r1.xy = r5.xy * r5.xy;
    r1.xy = r1.xy * r7.yz;
    r1.zw = -r4.xy * r7.yz + float2(1,1);
    r1.xy = r1.xy / r1.zw;
    r1.xy = r3.xy * r3.xy + r1.xy;
    r1.x = r1.x + r1.y;
    r1.xyz = float3(0.50022918,0.500110924,0.500561237) * r1.xxx;
    r3.x = saturate(dot(float3(2.37067437,-0.513885021,0.00529820006), r1.xyz));
    r3.y = saturate(dot(float3(-0.900040507,1.42530358,-0.0146949003), r1.xyz));
    r3.z = saturate(dot(float3(-0.470633805,0.0885813981,1.00939679), r1.xyz));
    r1.xyz = r3.xyz + -r6.xyz;
    r6.xyz = r3.www * r1.xyz + r6.xyz;
  }
  r1.x = r6.w * -5.55472994 + -6.98316002;
  r1.x = r1.x * r6.w;
  r1.x = exp2(r1.x);
  r1.yzw = float3(1,1,1) + -r6.xyz;
  r1.xyz = r1.yzw * r1.xxx + r6.xyz;
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r1.w = r4.w * r4.w;
  r3.xy = r4.ww * r4.ww + float2(0.330000013,0.0900000036);
  r3.x = r3.x + r3.x;
  r3.x = r1.w / r3.x;
  r3.x = 1 + -r3.x;
  r1.w = 0.449999988 * r1.w;
  r1.w = r1.w / r3.y;
  r3.y = max(-1, r5.w);
  r3.y = min(1, r3.y);
  r2.w = max(-1, r2.w);
  r2.w = min(1, r2.w);
  r3.z = 1 + -abs(r3.y);
  r3.z = sqrt(r3.z);
  r3.w = abs(r3.y) * -0.0187292993 + 0.0742610022;
  r3.w = r3.w * abs(r3.y) + -0.212114394;
  r3.w = r3.w * abs(r3.y) + 1.57072878;
  r4.x = r3.w * r3.z;
  r4.x = r4.x * -2 + 3.14159274;
  r4.y = cmp(r3.y < -r3.y);
  r4.x = r4.y ? r4.x : 0;
  r3.z = r3.w * r3.z + r4.x;
  r3.w = 1 + -abs(r2.w);
  r3.w = sqrt(r3.w);
  r4.x = abs(r2.w) * -0.0187292993 + 0.0742610022;
  r4.x = r4.x * abs(r2.w) + -0.212114394;
  r4.x = r4.x * abs(r2.w) + 1.57072878;
  r4.y = r4.x * r3.w;
  r4.y = r4.y * -2 + 3.14159274;
  r2.w = cmp(r2.w < -r2.w);
  r2.w = r2.w ? r4.y : 0;
  r2.w = r4.x * r3.w + r2.w;
  r3.w = max(r2.w, r3.z);
  r4.x = min(r2.w, r3.z);
  r2.w = r3.z + -r2.w;
  r2.w = cos(r2.w);
  r2.w = max(0, r2.w);
  r1.w = r2.w * r1.w;
  r2.w = sin(r3.w);
  r1.w = r2.w * r1.w;
  sincos(r4.x, r4.x, r5.x);
  r2.w = r4.x / r5.x;
  r1.w = r1.w * r2.w + r3.x;
  r2.w = max(0, r3.y);
  r1.xyz = v2.xyz * r1.xyz;
  r1.xyzw = r2.xyzw * r1.xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = float3(7.61524207e-005,7.61524207e-005,7.61524207e-005) * r1.xyz;
  r0.xyz = r0.xyz * v2.xyz + r1.xyz;
  r0.xyz = r7.xxx * r0.xyz;
  o0.xyz = r0.www * r0.xyz;
  o0.xyz *= RtLocalLightVisibility(int2(v0.xy), v3.xyz);
  o0.w = 1;
  return;
}